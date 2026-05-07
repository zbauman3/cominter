https://www.sameskydevices.com/product/resource/cma-4544pf-w.pdf

https://www.ti.com/lit/ds/symlink/tl071.pdf?HQS=dis-dk-null-digikeymode-dsf-pf-null-wwe&ts=1721694323530

https://www.ti.com/lit/ds/symlink/lm386.pdf?HQS=dis-dk-null-digikeymode-dsf-pf-null-wwe&ts=1774327252619&ref_url=https%253A%252F%252Fwww.flux.ai%252F

https://cdn-learn.adafruit.com/assets/assets/000/127/901/original/adafruit_products_Adafruit_ItsyBitsy_ESP32_PrettyPinsPDF-100.jpg?1708550028



- MCP6002
  - DC bias
    - min/max is 0.3v from VSS/VDD
    - so bias should be `((3.3 − 0.6) / 2) + 0.3` = `1.65v`
    - 5v voltage divider of 2-to-1 gives `1.666v`. using that.