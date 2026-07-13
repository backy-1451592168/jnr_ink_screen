# PCB 工程文件说明

硬件基于 [InkTime](https://github.com/dai-hongtao/InkTime/tree/main)，用嘉立创 EDA 设计，走嘉立创免费打板（PCB 制板 + 可选 SMT 贴片）。

## 文件一览

| 文件 | 用途 | 嘉立创下单时上传位置 |
| --- | --- | --- |
| `jnr_lnk_screen_EDA.zip` / `.epro` | 嘉立创 EDA 源工程（原理图 + PCB） | 不上传；本地打开改板用 |
| `Gerber_PCB1_2026-07-12.zip` | Gerber 光绘包（制板数据） | **PCB 下单 → 上传 Gerber** |
| `BOM.csv` | 物料清单（元器件型号 / 立创编号） | **SMT 贴片 → 上传 BOM** |
| `PickAndPlace.xlsx` | 坐标文件（贴片机放件位置） | **SMT 贴片 → 上传坐标文件** |

---

### `jnr_lnk_screen_EDA.zip` / `jnr_lnk_screen_EDA.epro`

嘉立创 EDA 专业版工程包（二者内容相同；`.epro` 本质就是 zip）。

**导入方式**（开始页 → 导入嘉立创EDA(专业版)）：

- 选 `jnr_lnk_screen_EDA.epro` 或 `jnr_lnk_screen_EDA.zip` 均可
- 包内根目录必须直接是 `project.json`、`SHEET/`、`PCB/`……**不能**再套一层 `jnr_lnk_screen_EDA/` 文件夹，否则会报「文件格式不正确」

本地改板：用专业版打开后改原理图/PCB，再「另存为(本地)」导出；或直接编辑 `jnr_lnk_screen_EDA/` 目录后，在该目录内重新打扁平压缩包。

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

### WS2812 指示灯接口（H7）

板底左侧、电源焊盘（G/5V/B+/G）右侧新增 **H7**（1×4P 2.54mm 排针，与灯珠脚序一致）：

| H7 脚 | 丝印 | 连接 |
| --- | --- | --- |
| 1 | DOUT | 空（可串下颗灯） |
| 2 | VBAT | 电池电压（约 3.7～4.2V，满足灯珠 ≥3.5V） |
| 3 | GND | 地 |
| 4 | DIN | **GPIO21** |

固件数据脚请用 `PIN_RGB_LED=21`。

> **注意**：本板为 ESP32-S3 + OPI PSRAM（N8R8/N16R8），**GPIO33–37 已被 PSRAM 占用**，不可接 LED/按键。按键现为 GPIO4/5/6。
改完原理图/PCB 后需在嘉立创 EDA 里再导出 Gerber，替换本目录旧光绘包再下单。

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

改板流程：用 EDA 打开 `jnr_lnk_screen_EDA.zip` → 修改 → 重新导出 Gerber / BOM / 坐标，替换本目录对应文件即可。
