# Cesium 离线地图需求技术文档阅读清单

生成日期：2026-07-07  
适用需求：UE 端通过本地 tile server 在在线/离线地图源之间切换；离线环境只要求 2D 卫星影像流式加载，同时保留未来扩展 3D Tiles、更多图层/格式的接口。

## 结论先行

实现前必须先拿到“离线地图数据源和授权方式”的项目结论，但开发人员不需要把法律/服务条款作为必读技术文档。技术实现侧只需要明确：离线影像数据以什么格式交付、覆盖范围是多少、zoom 范围是多少、是否需要署名/水印、是否允许切片后由本地 HTTP server 读取。稳妥实现路径是把已确认可离线使用的卫星影像，通过 GDAL 等工具切成 XYZ/TMS/MBTiles，再由本地 server 供 Cesium for Unreal 读取。

本项目已有配置入口：

- `Config/DefaultEngine.ini`：`[CesiumTileServer] UseLocalTileServer=false`，`LocalTileServerUrl=http://localhost:8070`
- `Source/UE5DroneControl/DroneOps/Control/DroneOpsGameMode.cpp`：`ApplyCesiumTileServerConfig()` 会在 `UseLocalTileServer=true` 时切换 Cesium tileset 和 raster overlay。
- 当前代码里，如果 `LocalTileServerUrl` 是裸地址 `http://localhost:8070`，影像模板会自动拼成 `http://localhost:8070/{z}/{x}/{y}.png`，3D Tiles 入口会自动拼成 `http://localhost:8070/tileset.json`。如果希望使用 `/tiles/{z}/{x}/{y}.png`，可以把配置改成完整模板 `http://localhost:8070/tiles/{z}/{x}/{y}.png`，或让 server 同时支持两种路径。

## 必读文档

| 优先级 | 文档 | 为什么要读 |
|---|---|---|
| P0 | Cesium for Unreal `ACesium3DTileset` API：<https://cesium.com/learn/cesium-unreal/ref-doc/classACesium3DTileset.html> | UE 端切换 `FromUrl`、`SetUrl()`、`RefreshTileset()` 的官方 API 依据。 |
| P0 | Cesium for Unreal `UCesiumUrlTemplateRasterOverlay` API：<https://cesium.com/learn/cesium-unreal/ref-doc/classUCesiumUrlTemplateRasterOverlay.html> | 2D 影像流式加载的核心。确认 `{x}`、`{y}`、`{z}`、`{reverseY}` 等模板变量和 `Refresh()` 行为。 |
| P0 | 3D Tiles Specification：<https://github.com/CesiumGS/3d-tiles/tree/main/specification> | `/tileset.json` 的权威格式依据。即使当前只做 2D 影像，也要让 server 的 3D Tiles 入口具备清晰扩展边界。 |
| P1 | GDAL `gdal2tiles`：<https://gdal.org/en/stable/programs/gdal2tiles.html> | 将 GeoTIFF/正射影像切成可服务瓦片的官方工具文档。重点读 `--xyz`、`--zoom`、`--tilesize`、`--tiledriver`、投影与重采样。 |
| P1 | MBTiles Specification：<https://github.com/mapbox/mbtiles-spec> | 如果离线数据用单文件包，MBTiles 是常见格式。重点确认 SQLite、Spherical Mercator、metadata、image tiles 的约束。 |
| P1 | OSM Slippy Map Tilenames：<https://wiki.openstreetmap.org/wiki/Slippy_map_tilenames> | 学习常见 XYZ `{z}/{x}/{y}` 瓦片编号、Web Mercator、经纬度到瓦片坐标的换算。 |
| P1 | Cesium for Unreal `UCesiumTileMapServiceRasterOverlay` API：<https://cesium.com/learn/cesium-unreal/ref-doc/classUCesiumTileMapServiceRasterOverlay.html> | 未来若 server 提供 TMS，需要理解 `Url`、`MinimumLevel`、`MaximumLevel` 与 `tilemapresource.xml`。 |
| P1 | Cesium for Unreal `UCesiumWebMapTileServiceRasterOverlay` API：<https://cesium.com/learn/cesium-unreal/ref-doc/classUCesiumWebMapTileServiceRasterOverlay.html> | 未来若扩展 WMTS，需要理解 `BaseUrl`、`Layer`、`Style`、`TileMatrixSetID`。 |

## 需要学习的核心知识

### 1. 离线数据输入

必须能回答：

- 离线卫星影像以什么格式交付：GeoTIFF、COG、XYZ 目录、TMS 目录、MBTiles、其他？
- 覆盖范围：经纬度 bbox 或目标区域边界。
- zoom 范围：最小级别、最大级别、目标分辨率。
- tile size：256 或 512。
- 坐标系：原始数据 EPSG 代码，是否需要转 Web Mercator / EPSG:3857。
- 是否需要在 UI 或 metadata 中展示 attribution / copyright 文案。

建议由项目侧提供一份非法律化的数据说明文件，例如 `offline-map-dataset.json`，server 的 `/health` 或 `/metadata` 可返回数据集名称、版本、覆盖范围、zoom 范围、tile scheme、tile size。

### 2. 2D 影像瓦片格式

本需求只要求 2D 卫星影像流式加载，优先掌握：

- Web Mercator / EPSG:3857 与经纬度 / EPSG:4326 的区别。
- XYZ 与 TMS 的 Y 轴方向差异：XYZ 常见约定是 z=0 顶层，y=0 在北侧；TMS 常见约定是 y=0 在南侧。
- Cesium URL Template 的 `{y}` 与 `{reverseY}` 行为要实测确认。项目当前默认拼 `/{z}/{x}/{y}.png`，如果数据是 GDAL `--xyz` 生成的北向 y，需要用小范围瓦片验证是否上下翻转。
- tile size：常见 256x256，也可能 512x512；UE/Cesium overlay 的 TileWidth/TileHeight 要和数据一致。
- 空瓦片处理：404、透明 PNG、占位图三种策略要统一。离线目标区域外建议 404 或透明瓦片，并明确日志级别。

### 3. 离线数据生产链路

建议学习并验证一条最小闭环：

1. 取得项目确认可使用的影像源，例如 GeoTIFF/COG。
2. 使用 GDAL 重投影到 Web Mercator，必要时用 `gdalwarp`。
3. 使用 `gdal2tiles --xyz --zoom=min-max --tilesize=256` 或等价命令生成 `{z}/{x}/{y}.png`。
4. 可选：打包为 MBTiles，server 从 SQLite 读取并按 URL 返回。
5. 用浏览器或 curl 请求少量瓦片，检查 MIME、大小、坐标方向、zoom 范围。
6. UE 设置 `UseLocalTileServer=true`，断网验证目标区域加载。

### 4. 本地 tile server 能力

server 至少要支持：

- `GET /health`：返回 200 和 JSON，例如 `{"status":"ok","datasets":[...]}`。
- `GET /tiles/{z}/{x}/{y}.png`：返回影像瓦片，正确设置 `Content-Type: image/png`。
- 兼容本项目当前裸地址拼接：建议同时支持 `GET /{z}/{x}/{y}.png`，或修改配置为完整模板。
- `GET /tileset.json`：返回 3D Tiles 入口。当前只做 2D 时，可返回一个明确的空/占位 tileset 或配置为实际离线 3D Tiles 数据；UE 端不应因 server 未启动或 tileset 不存在崩溃。
- 日志：启动端口、数据目录、zoom 范围、未命中瓦片、异常请求、数据说明元数据缺失。

建议预留扩展接口：

- `/metadata`：数据集名称、bounds、minZoom、maxZoom、tileSize、scheme、attribution。
- `/layers`：未来多图层列表。
- `/tiles/{layer}/{z}/{x}/{y}.{format}`：未来多源、多格式。
- `/3dtiles/{dataset}/tileset.json`：未来多 3D Tiles 数据集。

### 5. UE / Cesium 接入点

本项目必须读：

- `Config/DefaultEngine.ini`
- `Source/UE5DroneControl/DroneOps/Control/DroneOpsGameMode.cpp`
- `Source/UE5DroneControl/DroneOps/Control/DroneOpsGameMode.h`

重点理解：

- `UseLocalTileServer=false`：保留在线 Cesium/Google 源。
- `UseLocalTileServer=true`：遍历 `ACesium3DTileset`，把 tileset source 改为 URL，并刷新 URL-based raster overlay。
- 当前 runtime 可切换的 overlay 类型包括：
  - `UCesiumUrlTemplateRasterOverlay`
  - `UCesiumTileMapServiceRasterOverlay`
  - `UCesiumWebMapTileServiceRasterOverlay`
- 其他 overlay 类型只打日志，不应强行修改。

建议补强点：

- 本地 server 未启动时，UE 在切换前可请求 `/health`，失败时输出明确日志并保留原在线源或显示离线不可用状态，不崩溃。
- 日志中打印最终 tileset URL、raster template URL、server health 结果。
- 如果只做 2D 离线影像，需确认关卡中 3D Tileset 的 URL 切换是否必要；否则可能因为 `/tileset.json` 无真实内容导致视觉结果或日志噪声不符合预期。

## 推荐阅读顺序

1. 先读项目代码：`DefaultEngine.ini` 和 `ApplyCesiumTileServerConfig()`，确认现有 URL 拼接规则。
2. 再读 Cesium for Unreal Raster Overlay API，确定 2D 影像如何流式加载。
3. 再读 XYZ/TMS、GDAL、MBTiles，确定离线数据如何生产和存储。
4. 最后读 3D Tiles Specification，只实现 `/tileset.json` 的最小兼容入口，并为未来 3D 数据扩展留路径。

## 验收前检查清单

- 项目侧已提供离线数据说明：格式、bounds、zoom 范围、tile size、坐标系。
- server 默认监听 `http://localhost:8070`。
- `/health` 可用，server 启动日志包含数据目录、bounds、zoom 范围。
- `/tiles/{z}/{x}/{y}.png` 可用；如沿用项目裸地址，也支持 `/{z}/{x}/{y}.png`。
- `/tileset.json` 可用，返回合法 JSON；如暂无 3D 数据，行为和 UE 日志可解释。
- `UseLocalTileServer=false` 时在线源正常。
- `UseLocalTileServer=true` 且 server 正常时，断网进入目标区域可加载影像。
- `UseLocalTileServer=true` 且 server 未启动时，UE 有明确日志，不崩溃。
- 抽样检查瓦片没有上下翻转、经纬度偏移、zoom 错层、黑边或透明边异常。
- attribution / copyright 在 UI 或日志/metadata 中按项目要求保留。
