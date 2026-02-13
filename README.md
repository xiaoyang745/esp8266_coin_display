---

# ESP8266 Coin Display (240×240 TFT)

这是我自己用 ESP8266 做的一个小屏幕比特币（币安交易所 API）币种显示器：
能显示单币 / 三币 / 持仓盈亏，还能在单币模式画一个迷你的 10 根 K 线。
另外做了一个简单的 Web 页面，手机/电脑打开就能改币种、改小数位，并把 API 地址和飞书 Webhook 填进去。

> 这个仓库不会包含你的真实 API / Webhook。它们通过网页输入，保存到 EEPROM，重启也还在。

---

## 功能

### Single (Kline)

显示 1 个币种价格 + 10 根 1h K 线（迷你版）

<a href="https://github.com/user-attachments/assets/7dc2a67a-1c5e-48b3-a273-079f75d1ed8b">
  <img src="https://github.com/user-attachments/assets/7dc2a67a-1c5e-48b3-a273-079f75d1ed8b" width="260">
</a>

### Triple

同屏 3 个币种价格

<a href="https://github.com/user-attachments/assets/612d3676-f1d2-4b89-9358-7337e504d292">
  <img src="https://github.com/user-attachments/assets/612d3676-f1d2-4b89-9358-7337e504d292" width="260">
</a>

### Holdings P&L

同屏 3 个持仓：当前价、盈亏 USDT、盈亏百分比

<a href="https://github.com/user-attachments/assets/f026a0e1-9393-4a31-a9f4-c835e95c1de0">
  <img src="https://github.com/user-attachments/assets/f026a0e1-9393-4a31-a9f4-c835e95c1de0" width="260">
</a>

### Web 配置页面（重点）

* 切换模式
* 修改币种
* 设置小数位
* 输入并保存 API Base 和 Feishu Webhook
* 一键测试推送

<img src="https://github.com/user-attachments/assets/a65b2724-6f24-4354-a5b4-c299d280545b" width="900">

### 设备状态接口

* GET /sys 返回设备资源 JSON
* GET /push 手动推送一次到飞书（如果 webhook 已配置）

---

## 硬件准备

* ESP8266 开发板（NodeMCU / Wemos D1 mini 都行）
* 240×240 TFT 屏（常见的 ST7789）
* TFT_eSPI 已按你的屏幕接线配置好
* 背光脚：代码默认 TFT_BL = 5，并且 LOW 点亮（如果你板子不一样，改一下即可）

---

## 软件依赖

* Arduino IDE 或 PlatformIO
* ESP8266 core
* 依赖库：

  * WiFiManager
  * ArduinoJson
  * TFT_eSPI
  * ESP8266HTTPClient
  * BearSSL（ESP8266 core 自带）

---

## 如何使用

1. 用 Arduino IDE 打开 .ino，选对板子和串口，烧录
2. 上电后会开热点：BTC_Display
3. 用手机连上热点，按 WiFiManager 提示选择你的 WiFi 并输入密码
4. 连上网后，串口里会看到设备 IP（也可以路由器里找）
5. 浏览器打开 [http://设备IP/](http://设备IP/)
6. 在页面顶部先填：

   * API Base（你自己的行情 API 服务地址）
   * Webhook（可选，填了才会飞书推送）
7. 点击 Save 保存配置，然后随便切模式试一下

---

## API 服务怎么搭（教程）

这个项目不直接连交易所，而是从你自己的一层 API 拿数据。
这样 ESP8266 端很轻，接口也更可控（你想换数据源随时换）。

### 需要实现的接口

1. 获取价格
   GET {API_BASE}/price?symbol=BTCUSDT

返回格式：
{
"price": 12345.67
}

2. 获取 K 线
   GET {API_BASE}/klines?symbol=BTCUSDT&interval=1h&limit=10

返回格式：数组，每项至少要有下面索引（和 Binance 的 klines 结构一致）

* [1] open
* [2] high
* [3] low
* [4] close

示例（只展示结构）：
[
[0, "100", "110", "90", "105"],
[0, "105", "120", "100", "115"]
]

说明：
interval 固定用 1h，limit 固定用 10。
你要做别的周期也可以，自己改 URL 参数。

---

### Node.js API 示例

适合本地跑 / VPS 跑，直接转 Binance 的公开接口。

1. 初始化项目
   mkdir coin-api && cd coin-api
   npm init -y
   npm i express axios

2. 新建 server.js
   （此处保持你原来的代码块即可）

3. 启动
   node server.js

4. 测试（本机）
   curl "[http://127.0.0.1:8000/price?symbol=BTCUSDT](http://127.0.0.1:8000/price?symbol=BTCUSDT)"
   curl "[http://127.0.0.1:8000/klines?symbol=BTCUSDT&interval=1h&limit=10](http://127.0.0.1:8000/klines?symbol=BTCUSDT&interval=1h&limit=10)"

如果你跑在 VPS 上，记得放行端口（比如 8000），然后 ESP8266 网页里填：
[http://你的VPSIP:8000](http://你的VPSIP:8000)

---

## 飞书 Webhook 怎么配（教程）

1. 在飞书群里添加一个“自定义机器人”
2. 拿到它生成的 webhook URL（类似 [https://open.feishu.cn/open-apis/bot/v2/hook/...）](https://open.feishu.cn/open-apis/bot/v2/hook/...）)
3. 打开设备 Web 页面（[http://设备IP/）](http://设备IP/）)
4. 把 webhook 粘贴进去，点 Save
5. 点页面里的 Test push，群里能收到一条设备状态就说明 OK

不填 webhook 也没关系：项目照常运行，只是不会推送。

---

## 路由 & 接口清单

* / （Web 控制台）
* /mode?m=single|triple|holdings
* /single?sym=BTCUSDT
* /singleDec?d=0..6
* /triple?c0=...&c1=...&c2=...&d0=..&d1=..&d2=..
* /holdings?s0=...&s1=...&s2=...&b0=..&b1=..&b2=..&a0=..&a1=..&a2=..&d0=..&d1=..&d2=..
* /cfg（GET：查看当前 API/Webhook；GET/POST：保存配置）
* /sys（设备资源 JSON）
* /push（推送一次到飞书）

---
