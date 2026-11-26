import asyncio
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from luna_weather_setup import WeatherSession, make_settings, parse_cities, provision
from luna_ble_protocol import ProtocolError, Reassembler, encode_fragments

TOKEN='1'*32
BOOT='0123456789abcdef'

def fields(**changes):
    return make_settings(**(dict(ssid='Example Network',password='example-only-123',location='上海',latitude=31.2,longitude=121.5,setup_id=TOKEN)|changes))

def status(**changes):
    return dict(setup_id=TOKEN,state='saved',saved=True,error=0,wifi_configured=True,
                wifi_online=True,weather_configured=True,weather_available=True,weather_cached=False)|changes

class ValidationTests(unittest.TestCase):
    def test_byte_boundaries_password_and_coordinates(self):
        self.assertEqual(fields(ssid='猫'*10)['ssid'],'猫'*10)
        fields(ssid='x'*32,password='a'*64,location='猫'*15)
        for changes in (dict(ssid='猫'*11),dict(location='猫'*16),dict(password='x'*64),
                        dict(password='中文密码1234'),dict(password='short'),dict(latitude=True),
                        dict(longitude=float('nan')),dict(latitude=91),dict(ssid='a\0b'),dict(setup_id='bad')):
            with self.subTest(changes=changes),self.assertRaises(ValueError): fields(**changes)
        fields(password='')

    def test_geocoding_never_silently_picks_ambiguous_or_invalid_city(self):
        cities=parse_cities({'results':[{'name':'上海','latitude':31.2,'longitude':121.5,'country':'中国'},
                                        {'name':'Broken','latitude':True,'longitude':1},
                                        {'name':'Bad\nCity','latitude':1,'longitude':1}]})
        self.assertEqual([c['name'] for c in cities],['上海'])
        self.assertEqual(parse_cities({}),[])
        with self.assertRaises(ValueError): parse_cities({'results':{}})

class WireClient:
    mtu_size=256
    def __init__(self, payload=253):
        self.receiver=Reassembler();self.kinds=[];self.payload=payload
        self.override={};self.feature=True;self.responses=[status()];self.accepted=True
    async def write_gatt_char(self, characteristic, data, response):
        self.assert_packet(data)
        message=self.receiver.feed(bytes(data))
        if message is None:return
        req=json.loads(message.payload.decode());kind=req['type'];self.kinds.append(kind)
        reply={'v':1,'session':req['session'],'boot_id':BOOT}
        if kind=='hello':reply.update(type='hello',name='Luna',max_message=4096,att_payload=self.payload,features=['weather_setup'] if self.feature else [])
        elif kind=='weather_configure':reply.update(type='weather_configure_result',accepted=self.accepted)
        elif kind=='weather_status':
            state=self.responses[0]
            if len(self.responses)>1:self.responses.pop(0)
            reply.update(type='weather_status',**state)
        reply.update(self.override)
        for fragment in encode_fragments(json.dumps(reply).encode(),message.message_id,self.payload):
            self.session.notification(None,bytearray(fragment))
    def assert_packet(self,data):
        if len(data)>max(20,self.payload):raise AssertionError('MTU exceeded')

class ProvisionTests(unittest.IsolatedAsyncioTestCase):
    async def setup_session(self,payload=253):
        client=WireClient(payload);session=WeatherSession(client,request_timeout=.01);client.session=session
        await session.request('hello')
        return client,session

    async def test_full_fragmented_setup_small_and_large_mtu(self):
        for payload in (20,253):
            client,session=await self.setup_session(payload)
            client.responses=[status(state='saving',saved=False,wifi_online=False),status()]
            value=fields()
            result=await provision(session,value,sleep=lambda _:asyncio.sleep(0))
            self.assertTrue(result['weather_available']);self.assertNotIn('password',value)
            self.assertEqual(client.kinds.count('weather_configure'),1)
            self.assertFalse(any('music' in k or k=='state_snapshot' for k in client.kinds))

    async def test_old_firmware_rejected_before_write(self):
        client=WireClient();client.feature=False;session=WeatherSession(client);client.session=session
        with self.assertRaises(ProtocolError):await session.request('hello')
        self.assertEqual(client.kinds,['hello'])

    async def test_rejected_queue_and_failed_storage_are_not_saved(self):
        client,session=await self.setup_session();client.accepted=False
        with self.assertRaises(RuntimeError):await provision(session,fields())
        self.assertEqual(client.kinds.count('weather_configure'),1)
        client,session=await self.setup_session();client.responses=[status(state='save_failed',saved=False,error=257)]
        with self.assertRaises(RuntimeError):await provision(session,fields())

    async def test_rpc_failure_is_saved_but_not_claimed_online(self):
        client,session=await self.setup_session();client.responses=[status(state='apply_failed',wifi_online=False,error=-1)]
        result=await provision(session,fields())
        self.assertTrue(result['saved']);self.assertFalse(result['wifi_online'])

    async def test_saved_offline_or_cache_is_not_claimed_fresh(self):
        for state in (status(wifi_online=False),status(weather_cached=True)):
            client,session=await self.setup_session();client.responses=[state]
            clock=iter((0,0,5))
            result=await provision(session,fields(),wait_seconds=2,sleep=lambda _:asyncio.sleep(0),monotonic=lambda:next(clock))
            self.assertEqual(result,state|{'v':1,'type':'weather_status','session':session.session,'boot_id':BOOT})

    async def test_timeout_never_replays_and_scrubs_caller_password(self):
        client,session=await self.setup_session()
        async def lost(*args,**kwargs):raise ConnectionError('uncertain')
        client.write_gatt_char=lost
        value=fields()
        with self.assertRaises(ConnectionError):await provision(session,value)
        self.assertNotIn('password',value)
        with self.assertRaises(ConnectionError):await session.request('weather_configure',**fields())
        self.assertEqual(client.kinds,['hello'])

    async def test_boot_change_and_malformed_status_rejected(self):
        for override in ({'boot_id':'other'},{'saved':1},{'state':'invented'},{'setup_id':'private text'}):
            client,session=await self.setup_session();client.override=override
            with self.assertRaises(ProtocolError):await session.request('weather_status')
