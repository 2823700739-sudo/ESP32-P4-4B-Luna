# Luna 月亮小猫：原始图集与来源

生成方式：内置图像生成工具（不是 CLI/API key 工作流），透明背景。
角色由图像生成工具创建，不取自 GitHub 参考项目。当前形象与繁星交互已获用户确认，固件已使用转换后的内置资源；本页保留原始生成来源。

文件：`luna-mooncat-atlas.png`，PNG RGBA，1774×887，926491 字节。
SHA256：`e20220d8059f5185862d00055a305f828b5a4bf195e5b7225573ad202b925a2d`。
原生成文件保留，项目使用复制后的本文件，不依赖 Codex 的生成目录。

## 预览帧映射

按四列两行等比分格，在预览中用 CSS 最近邻缩放显示。

| 帧 | 预览用途 |
| --- | --- |
| 0 / 1 | 坐下、眨眼 |
| 2 / 3 | 两帧朝右走路；朝左在渲染时镜像 |
| 4 / 5 | 点击回应 |
| 6 / 7 | 角落睡觉；另加轻微呼吸 |

有透明通道且八格角色可辨，但生成器没有严格输出所请求的 1024×512 尺寸，
宽高不能按整数像素均分四列/两行，因此保留为原始来源图集，
预览按比例取格，未改写原图像像素。固件资源已完成转换并内置，
转换与校验见[固件素材说明](../../../../firmware/luna-panel/main/assets/README.md)。
当前没有 TF 动态资源加载功能，不把历史候选阶段的计划作为待完成承诺。

## 完整生成提示词

```text
Use case: stylized-concept. Asset type: original transparent pixel-art sprite atlas for Luna, a friendly ESP32 desktop companion with a gentle moon-night UI. Primary request: ONE precise 4-column by 2-row sprite sheet, canvas 1024x512 pixels, each cell 256x256, transparent background. Same tiny pale lavender moon kitten in every cell: round soft body, two triangular ears, tiny golden crescent marking on forehead, dark navy eyes, a curved fluffy tail, restrained ivory/lavender palette and a warm gold accent. Pixel art, hard square pixel clusters at consistent 4px grid, nearest-neighbor style, no soft outlines, no antialiasing, no halo, no shadows outside the character. Occupy only central ~150x150 pixels in each cell with generous transparent padding, every pose fully contained and aligned. Exactly eight poses laid out in reading order: top row: seated facing front eyes open, seated front blinking, walking to right left paw forward, walking to right right paw forward. Bottom row: cheerful tap response with raised paw, cheerful tap response eyes closed smile, curled sleeping profile eyes closed, same curled sleeping pose tiny breathing variation. Identity, proportions, scale and colors remain identical between cells. Atlas for state animation, not an illustrative poster. No cell borders, no labels, no text, no watermark, no stars floating outside the cat, no other characters. True transparent alpha.
```
