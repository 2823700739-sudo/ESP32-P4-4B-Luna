"""Interactive weather provisioning over Luna's already paired, authenticated BLE link.

No credentials in arguments, files or logs; no automatic pairing or media actions.
"""
from __future__ import annotations

import argparse
import asyncio
from datetime import datetime
import getpass
import json
import math
import re
import sys
import time
from urllib.parse import urlencode
from urllib.request import Request, urlopen
import uuid

from agent_instance import single_agent_instance
from luna_ble_probe import ProbeSession, connected_session
from luna_ble_link import discover_device
from luna_ble_protocol import ProtocolError


class WeatherSession(ProbeSession):
    REQUEST_TYPES = {**ProbeSession.REQUEST_TYPES,
                     'weather_configure': 'weather_configure_result', 'weather_status': 'weather_status'}

    def validate_reply(self, kind, reply):
        if kind == 'hello':
            features=reply.get('features')
            if not isinstance(features,list) or 'weather_setup' not in features:
                raise ProtocolError('Firmware has no weather setup; install the weather-setup firmware first')
        if kind not in ('weather_status', 'weather_configure'):
            return
        if reply.get('boot_id') != self.boot_id:
            raise ProtocolError('Device restarted; reconnect and read status before changing settings')
        if kind == 'weather_configure':
            if type(reply.get('accepted')) is not bool:
                raise ProtocolError('Invalid configuration acknowledgement')
            return
        if reply.get('state') not in {'idle', 'saving', 'saved', 'apply_failed', 'save_failed', 'unavailable'}:
            raise ProtocolError('Invalid setup state')
        token = reply.get('setup_id')
        if not isinstance(token, str) or (token and not re.fullmatch('[0-9a-f]{32}', token)):
            raise ProtocolError('Invalid setup identity')
        for field in ('saved', 'wifi_configured', 'wifi_online', 'weather_configured', 'weather_available', 'weather_cached'):
            if type(reply.get(field)) is not bool:
                raise ProtocolError('Invalid weather status')
        if type(reply.get('error')) is not int:
            raise ProtocolError('Invalid setup error')
        if reply['saved'] and (not token or reply['state'] not in {'saved', 'apply_failed'}):
            raise ProtocolError('Inconsistent saved status')


def valid_text(value, limit, empty=False):
    if not isinstance(value, str) or (not value and not empty) or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise ValueError('文本不能为空或包含控制字符。')
    try:
        size = len(value.encode('utf-8'))
    except UnicodeError as error:
        raise ValueError('文本编码无效。') from error
    if size > limit:
        raise ValueError(f'文本过长，最多 {limit} 个 UTF-8 字节。')
    return value


def make_settings(ssid, password, location, latitude, longitude, setup_id=None):
    valid_text(ssid, 32)
    valid_text(location, 47)
    valid_text(password, 64, empty=True)
    if any(not 32 <= ord(c) <= 126 for c in password):
        raise ValueError('Wi-Fi 密码需要使用可打印 ASCII 字符。')
    if password and not 8 <= len(password) <= 64:
        raise ValueError('Wi-Fi 密码需要 8–63 个字符，或 64 位十六进制密钥；开放网络留空。')
    if len(password) == 64 and not re.fullmatch('[0-9a-fA-F]{64}', password):
        raise ValueError('64 位密钥必须全部为十六进制字符。')
    for value, bound in ((latitude, 90), (longitude, 180)):
        if type(value) not in (int, float) or not math.isfinite(value) or not -bound <= value <= bound:
            raise ValueError('城市经纬度无效。')
    token = uuid.uuid4().hex if setup_id is None else setup_id
    if not isinstance(token, str) or re.fullmatch('[0-9a-f]{32}', token) is None:
        raise ValueError('配置编号无效。')
    return dict(setup_id=token, ssid=ssid, password=password, location=location,
                latitude=latitude, longitude=longitude)


def parse_cities(body):
    if not isinstance(body, dict) or not isinstance(body.get('results', []), list):
        raise ValueError('城市搜索响应无效。')
    cities = []
    for item in body.get('results', [])[:5]:
        if not isinstance(item, dict):
            continue
        try:
            name = valid_text(item.get('name'), 47)
            lat, lon = item['latitude'], item['longitude']
            make_settings('validation', '', name, lat, lon)
            label = ' / '.join(str(item[k]) for k in ('name', 'admin1', 'country') if isinstance(item.get(k), str))
            valid_text(label, 512)
            cities.append(dict(name=name, latitude=lat, longitude=lon, label=label))
        except (ValueError, KeyError):
            continue
    return cities


def search_cities(query):
    valid_text(query, 128)
    url = 'https://geocoding-api.open-meteo.com/v1/search?' + urlencode(dict(name=query, count=5, language='zh', format='json'))
    with urlopen(Request(url, headers={'User-Agent': 'Luna-weather-setup/1'}), timeout=15) as response:
        data = response.read(65537)
    if len(data) > 65536:
        raise ValueError('城市搜索响应过大。')
    return parse_cities(json.loads(data.decode('utf-8')))


def select_city():
    print('输入城市名会向 Open-Meteo 搜索；Wi-Fi 名称和密码不会发送给该服务。')
    print('输入 m 可手动填写城市名和经纬度；输入 q 取消。')
    while True:
        query = input('天气城市（例如 Shanghai / 北京）：').strip()
        if query.lower() == 'q':
            return None
        if query.lower() == 'm':
            try:
                name = valid_text(input('显示的城市名：').strip(), 47)
                lat = float(input('纬度（-90～90）：'))
                lon = float(input('经度（-180～180）：'))
                make_settings('validation', '', name, lat, lon)
                return dict(name=name, latitude=lat, longitude=lon)
            except ValueError as error:
                print(error)
                continue
        try:
            cities = search_cities(query)
        except Exception:
            print('城市查询失败。请检查网络、改用拼音/英文名，或输入 m 手动填写经纬度。')
            continue
        if not cities:
            print('未找到城市，请改用拼音/英文名，或输入 m。')
            continue
        for index, city in enumerate(cities, 1):
            print(f'{index}. {city["label"]}')
        choice = input('选择城市编号（回车重新搜索）：').strip()
        if choice.isdigit() and 1 <= int(choice) <= len(cities):
            return cities[int(choice) - 1]


def collect_settings():
    city = select_city()
    if city is None:
        return None
    while True:
        ssid = input('2.4 GHz Wi-Fi 名称（区分大小写）：')  # Preserve leading/trailing SSID spaces.
        password = getpass.getpass('Wi-Fi 密码（不显示，开放网络留空）：')
        try:
            fields = make_settings(ssid, password, city['name'], city['latitude'], city['longitude'])
        except ValueError as error:
            print(error)
            continue
        if not password and input('确认使用无密码的开放网络？[y/N]：').strip().lower() != 'y':
            continue
        print(f'即将保存城市：{city["name"]}；Wi-Fi：{ssid}。密码不会保存到电脑或显示。')
        return fields if input('保存到 Luna？[y/N]：').strip().lower() == 'y' else None


async def provision(session, fields, wait_seconds=90, sleep=asyncio.sleep, monotonic=time.monotonic):
    """Send exactly once. Polling never repeats a write after an uncertain outcome."""
    token = fields['setup_id']
    try:
        result = await session.request('weather_configure', **fields)
    finally:
        fields.pop('password', None)  # Python strings cannot promise physical memory erasure.
    if not result['accepted']:
        raise RuntimeError('设备暂不能接受配置，请稍后重新读取状态。')
    deadline = monotonic() + wait_seconds
    last = None
    while monotonic() < deadline:
        last = await session.request('weather_status')
        if last['setup_id'] == token:
            if last['state'] == 'save_failed':
                raise RuntimeError('设备保存配置失败，未确认保存；请保留原配置并检查设备存储。')
            if last['saved'] and last['state'] == 'apply_failed':
                return last
            if last['saved'] and last['wifi_online'] and last['weather_available'] and not last['weather_cached']:
                return last
        await sleep(2)
    if last and last['setup_id'] == token and last['saved']:
        return last
    raise TimeoutError('未确认配置保存结果。不要重复提交；重新运行先查看设备状态。')


async def run(args):
    device = await discover_device(args)
    async with connected_session(device, session_factory=WeatherSession) as session:
        await session.request('hello')  # Reject old firmware before asking for a password.
        status = await session.request('weather_status')
        if status['state'] in {'saving', 'unavailable'}:
            raise RuntimeError('设备正在初始化或配置服务不可用，请稍后重试。')
        if status['wifi_configured'] and status['weather_configured']:
            if await asyncio.to_thread(input, '设备已有天气配置，需要更改吗？[y/N]：') not in ('y', 'Y'):
                print('保留设备已有配置。')
                return 0
        fields = await asyncio.to_thread(collect_settings)
        if fields is None:
            print('已取消，未修改设备配置。')
            return 0
        offset = int(datetime.now().astimezone().utcoffset().total_seconds() // 60)
        try:
            await session.request('time_sync', epoch_ms=int(time.time() * 1000), timezone_offset_minutes=offset)
            status = await provision(session, fields, args.wait_seconds)
        finally:
            fields.clear()
        print('天气配置已保存到 Luna，重启后保留。')
        if status['state'] == 'apply_failed':
            print('设备 Wi-Fi 初始化未成功；请检查本板已有 C6 固件兼容性，不要自动刷 C6。')
        elif not status['wifi_online']:
            print('尚未连上 Wi-Fi。请检查 2.4 GHz 网络、名称和密码；可重新运行本向导更改。')
        elif not status['weather_available'] or status['weather_cached']:
            print('Wi-Fi 已连接，尚未取得最新天气；设备会继续重试，请检查网络是否可访问 Open-Meteo。')
        else:
            print('Wi-Fi 已连接，设备已获取最新天气。')
    return 0


def main():
    parser = argparse.ArgumentParser(description='Luna weather setup; Wi-Fi password is prompted privately, never a CLI argument.')
    parser.add_argument('--address', help='Select one already paired Luna when multiple boards advertise')
    parser.add_argument('--scan-seconds', type=float, default=5)
    parser.add_argument('--wait-seconds', type=int, default=90)
    args = parser.parse_args()
    if not 1 <= args.scan_seconds <= 30 or not 5 <= args.wait_seconds <= 120:
        parser.error('Invalid scan/wait duration')
    if not sys.stdin.isatty():
        print('请在交互式终端运行天气设置；不从文件或管道读取 Wi-Fi 密码。')
        return 1
    try:
        with single_agent_instance('Local\\LunaBleLink') as acquired:
            if not acquired:
                print('Luna 连接正在被其他进程使用。请先关闭另一个前台连接，再通过当前文件夹的 Configure-Weather.cmd 配置。')
                return 1
            return asyncio.run(run(args))
    except KeyboardInterrupt:
        print('已取消。若发送后中断，下次先查看设备已有配置，不自动重放。')
        return 130
    except Exception as error:
        # Never print arbitrary BLE/HTTP exception text which might echo a request.
        if isinstance(error, (ProtocolError, RuntimeError)) and error.args:
            known = str(error)
            safe = ('Firmware has no weather setup', '设备', '未确认配置')
            print(known if known.startswith(safe) else '天气配置失败，请检查设备供电、手动配对和固件版本。')
        else:
            print('天气配置未完成；请检查供电、手动配对、固件和网络。发送后出错请先读状态，勿反复提交。')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
