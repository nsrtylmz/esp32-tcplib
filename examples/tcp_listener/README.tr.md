tcp_listener örneği
===================

[English](README.md) | **Türkçe**

En fazla üç TCP echo sunucusu (`TcpListener`) çalıştırır. Her biri kendi
portunda dinler ve isteğe bağlı olarak yalnızca tek bir ağ arayüzünden bağlantı
kabul eder (`TcpOptions::netif_key`):

| Sunucu | Port | Bağlantı kabul ettiği yer | `netif_key` |
|---|---|---|---|
| 1 | 5001 | yalnızca Wi-Fi station | `WIFI_STA_DEF` |
| 2 | 5002 | yalnızca Ethernet | `ETH_DEF` |
| 3 | 5003 | ikisi de | `""` (herhangi biri) |

Bir arayüze bağlanan dinleyici yalnızca o arayüzden gelen bağlantıları görür;
lwIP diğerlerine sıfırlama (RST) ile cevap verir ve istemci "connection
refused" alır. Kabul edilen her bağlantı kendi task'ında çalışır (`TcpClient`'ın
sahibi o task'tır), geldiği arayüzü bildiren bir selam mesajı alır ve sonra
gönderdiği her şey ona geri gönderilir:

```
server3 hello 172.16.10.30 via WIFI_STA_DEF
```

ESP32 aynı bilgiyi loglar:

```
I server3: connection from 192.168.1.100 port 50016 on ETH_DEF (to 192.168.1.101 port 5003), 1 open
```

Kütüphaneyi depo kökünden (`../..`) kullanır.

Dosyalar:

- `main/main.cpp`: sunucu ve bağlantı task'ları
- `main/server_config.h`: menuconfig ayarlarını bir tabloya çevirir
- `main/Kconfig.projbuild`: menuconfig ayarları
- `tools/tcp_listener_test.py`: bilgisayar için test istemcisi

## Ayarlar

`idf.py menuconfig`:

- **Example Connection Configuration**: *Connect using WiFi interface*
  (ağ adı, şifre) ve *Connect using Ethernet interface* seçeneklerini açın (W5500
  pinleri: `sdkconfig.w5500` ve `../tcp_multi_client/README.tr.md`).
  *Preferred IPv6 Type* ayarını **Local Link** bırakın.
- **Listener Configuration**:
  - *Max connections per server* (varsayılan 3): bir fazlası `server<N> busy`
    mesajı alıp kapatılır
  - *Close idle connections after (s)* (varsayılan 60, 0 = hiçbir zaman)
  - **Server 1 / Server 2 / Server 3**, her birinde *Enable*, *Port* ve
    *Accept connections from*: Any interface, yalnızca Wi-Fi station, yalnızca
    Ethernet, ya da elle girilen bir esp_netif anahtarı (örneğin `WIFI_AP_DEF`)

Arayüzü olmayan bir sunucu `interface "ETH_DEF" does not exist ...` yazıp 5
saniyede bir tekrar dener; diğerleri çalışır.

`sdkconfig.defaults`, `CONFIG_LWIP_MAX_SOCKETS` değerini 16'ya çıkarır: üç
dinleyici artı her birine üçer bağlantı 12 soket eder (varsayılan sınır 10).

## Bilgisayarda test

`tools/tcp_listener_test.py` her sunucuya her arayüzden bağlanır; kabul/red
durumunu, selam mesajındaki arayüzü ve echo'yu kontrol eder, sonra bir tablo
yazdırıp 0 (GEÇTİ) ya da 1 (KALDI) çıkış koduyla biter. Python 3.8+, yalnızca
standart kütüphane. ESP32'nin her ağdaki adresini verin (açılışta
`example_common: - IPv4 address: ...` olarak yazılır):

```
python tools/tcp_listener_test.py --wifi 172.16.10.65 --eth 192.168.1.101 --max-clients 3
```

ESP32 + W5500 üzerindeki sonuç (bilgisayar iki ağa da bağlı):

```
server   port  via   address          expected        result          detail
server1  5001  wifi  172.16.10.65     accepted        accepted  ok    server1 hello 172.16.10.30 via WIFI_STA_DEF
server1  5001  eth   192.168.1.101    refused         refused   ok    connection refused (RST)
server2  5002  wifi  172.16.10.65     refused         refused   ok    connection refused (RST)
server2  5002  eth   192.168.1.101    accepted        accepted  ok    server2 hello 192.168.1.100 via ETH_DEF
server3  5003  wifi  172.16.10.65     accepted        accepted  ok    server3 hello 172.16.10.30 via WIFI_STA_DEF
server3  5003  eth   192.168.1.101    accepted        accepted  ok    server3 hello 192.168.1.100 via ETH_DEF
server1  5001  wifi  172.16.10.65     busy after 3    busy      ok
server2  5002  eth   192.168.1.101    busy after 3    busy      ok
server3  5003  eth   192.168.1.101    busy after 3    busy      ok

PASS: every server accepted and refused exactly as expected
```

Seçenekler:

| Seçenek | Anlamı |
|---|---|
| `--wifi IP` / `--eth IP` | ESP32'nin her ağdaki adresi (en az biri) |
| `--ports 5001,5002,5003` | sunucu 1, 2, 3'ün portları |
| `--expect 1=wifi,2=eth,3=both` | her sunucunun nereden kabul etmesi gerektiği (`wifi`, `eth`, `both`, `none`); menuconfig'deki seçimleri değiştirince bunu da değiştirin |
| `--max-clients 3` | sunucu başına bağlantı sınırını da kontrol et |
| `--timeout 3` | her adım için saniye |

Bilgisayar ESP32'ye iki ağdan da ulaşabilmelidir (örneğin birinde Wi-Fi,
diğerinde Ethernet kablosu). Elle denemek için herhangi bir TCP aracı da
kullanılabilir (`nc`, Hercules, PuTTY raw modu): 5001..5003 portlarına bağlanıp
yazın.

## Derleme ve yükleme

```
idf.py set-target esp32
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.w5500" menuconfig
idf.py build flash monitor
```

`sdkconfig` depoda yoktur (Wi-Fi şifresini içerir). Yalnızca
`sdkconfig.defaults` ile yalnızca Wi-Fi'li bir derleme olur (sunucu 2 o zaman
`ETH_DEF` arayüzünün olmadığını bildirir); yukarıdaki gibi `sdkconfig.w5500`
eklemek test edilen W5500 Ethernet'i açar. İkisi de yalnızca `sdkconfig`
oluşturulurken uygulanır.
