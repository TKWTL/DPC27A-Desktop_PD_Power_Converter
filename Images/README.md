# Images

仓库图片素材按用途分目录:

| 目录 | 用途 | 被引用于 |
|---|---|---|
| `Product/` | 成品外观、接口、点亮界面 | 根 `README.md`、`Docs/USER_MANUAL.md` |
| `Assembly/` | 装配过程照片(按装配顺序) | `Docs/ASSEMBLY_GUIDE.md` |
| `Testing/` | 测试环境、波形、温升曲线 | `Docs/TEST_REPORT.md` |

命名建议:

- 装配类:现有实拍图用「序号-内容.jpg」形式(如 `1-背面贴片.jpg`、`13-锡桥连接.jpg`),
  序号即装配顺序,新增照片请沿用手写体前缀并保持序号连续;
- 成品类:`hero-front.jpg`、`hero-back.jpg`、`ports.jpg`、`dashboard-on.jpg`;
- 测试类:`pd-epr.png`、`thermal-step.png`、`fan-curve.png`、`ripple-full.png`。

约定:

- `Assembly/` 的 14 张实拍图已被 `Docs/ASSEMBLY_GUIDE.md` 全部引用(含 §0 索引表),
  新增照片请同时补进该索引表与对应章节;
- 建议单张 ≤ 500 KB(长边 ≤ 2000 px),避免仓库过大——现有实拍图多在 0.3–1.1 MB,
  如需瘦身可直接覆盖同名文件,文档无需改动;
- 测试原始截图/多组波形不必全部入库,只保留文档引用的精选图。
