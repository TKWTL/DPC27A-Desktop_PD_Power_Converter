# Images

仓库图片素材按用途分目录:

| 目录 | 用途 | 被引用于 |
|---|---|---|
| `Product/` | 成品外观、接口、点亮界面 | 根 `README.md`、`Docs/USER_MANUAL.md`、`Docs/ASSEMBLY_GUIDE.md` §14 烧录后判据 |
| `Assembly/` | 装配过程照片(按装配顺序) | `Docs/ASSEMBLY_GUIDE.md` |
| `Testing/` | 测试环境、波形、温升曲线 | `Docs/TEST_REPORT.md` |

命名建议:

- 装配类:现有实拍图用「序号-内容.jpg」形式(如 `1-背面贴片.jpg`、`15-WCH_ISP下载.jpg`),
  序号即装配顺序,新增照片请沿用手写体前缀并保持序号连续;
- 成品类:现为中文描述式命名,如 `主界面-5V输入.jpg`、`主界面-21V-6.35A输入.jpg`、
  `主界面-28V-5A输入.jpg`、`主菜单.jpg`、`系统设置界面.jpg`;
  **文件名不要带空格**,用 `-` 连接,否则部分 Markdown 预览(VS Code)解析 `%20` 会失败;
- 测试类:现为「序号-内容.jpg」,如 `0-连接SW3538作为电压的上电打印输出.jpg`(上电串口横幅);
  后续可沿用 `pd-epr.png`、`thermal-step.png`、`fan-curve.png`、`ripple-full.png` 之类英文名;
- 已把一张上电串口打印放在 `Testing/` 并接进 `Docs/ASSEMBLY_GUIDE.md` §13 作对照;

约定:

- `Assembly/` 的 16 张实拍图已被 `Docs/ASSEMBLY_GUIDE.md` 全部引用(含文末索引表),
  第 15/16 张(WCHISPStudio 下载、自制 WCH-LinkE 接线)同时被 `Docs/USER_MANUAL.md` §11 引用;
  新增照片请同时补进该索引表与对应章节;
- 建议单张 ≤ 500 KB(长边 ≤ 2000 px),避免仓库过大——现有实拍图多在 0.1–1.1 MB,
  如需瘦身可直接覆盖同名文件,文档无需改动;
- 测试原始截图/多组波形不必全部入库,只保留文档引用的精选图。
