# PCB 工程文件说明

硬件基于 [InkTime](https://github.com/dai-hongtao/InkTime/tree/main)，用嘉立创 EDA 设计，走嘉立创免费打板（PCB 制板 + 可选 SMT 贴片）。

## 文件一览

| 文件 | 用途 | 嘉立创下单时上传位置 |
| --- | --- | --- |
| `InkTime_JLC_EDA.zip` | 嘉立创 EDA 源工程（原理图 + PCB） | 不上传；本地打开改板用 |
| `Gerber_PCB1_2026-07-12.zip` | Gerber 光绘包（制板数据） | **PCB 下单 → 上传 Gerber** |
| `BOM.csv` | 物料清单（元器件型号 / 立创编号） | **SMT 贴片 → 上传 BOM** |
| `PickAndPlace.xlsx` | 坐标文件（贴片机放件位置） | **SMT 贴片 → 上传坐标文件** |

---

### `InkTime_JLC_EDA.zip`

嘉立创 EDA（EasyEDA）工程导出包，内含原理图（`.esch`）、PCB（`.epcb`）、符号与封装等。

- 用 [嘉立创 EDA 专业版 / 标准版](https://lceda.cn/) 打开后可改原理图、布线、再导出 Gerber / BOM / 坐标。
- **打板不需要传这个包**，只作为可编辑源文件留档。

### `Gerber_PCB1_2026-07-12.zip`

制板用的光绘文件，嘉立创下单「PCB」时上传。包内大致包括：

| 图层 | 含义 |
| --- | --- |
| `.GTL` / `.GBL` | 顶层 / 底层铜箔 |
| `.GTO` / `.GBO` | 顶层 / 底层丝印 |
| `.GTS` / `.GBS` | 顶层 / 底层阻焊 |
| `.GTP` | 顶层钢网（锡膏） |
| `.GKO` | 板框外形 |
| `.DRL` | 钻孔（通孔 / 过孔） |

免费打板通常只需这个包：选层数（本板双层）、板厚、阻焊颜色等即可。

### `BOM.csv`

物料清单（Bill of Materials）。列含位号、封装、型号、厂商，以及立创商城编号（`Supplier Part`，如 `C2913201`）。

- 只做**光板**（自己焊）：可不传，按表采购即可。
- 做**嘉立创 SMT 经济型贴片**：下单时上传，系统按立创编号匹配库存元器件。

当前主要物料：ESP32-S3-WROOM-1-N8R8、MIC5219 3.3V LDO、0603 阻容、轻触开关等。

### `PickAndPlace.xlsx`

贴片坐标文件（Centroid / CPL），标明每个元器件在板子上的 X/Y、旋转角度、正反面。

- 仅在选 **SMT 贴片** 时需要，与 `BOM.csv` 一起上传。
- 光板打板不用传。

---

## 嘉立创免费打板怎么用

**只打光板（自己焊）**

1. 打开 [嘉立创 PCB 下单](https://www.jlc.com/)
2. 上传 `Gerber_PCB1_2026-07-12.zip`
3. 确认层数、尺寸、阻焊色等 → 下单

**打板 + SMT 贴片**

1. 同上上传 Gerber 下 PCB 单
2. 勾选 SMT / 经济型贴片
3. 上传 `BOM.csv` + `PickAndPlace.xlsx`
4. 核对元器件匹配与贴片面 → 一起下单

改板流程：用 EDA 打开 `InkTime_JLC_EDA.zip` → 修改 → 重新导出 Gerber / BOM / 坐标，替换本目录对应文件即可。
