tcp_multi_client örneği
=======================

[English](README.md) | **Türkçe**

Aynı anda en fazla üç TCP istemcisi çalıştırır. Her istemcinin kendi task'ı ve
kendi `TcpClient` nesnesi vardır, ve her biri seçilen bir ağ arayüzüne
bağlanabilir (`TcpOptions::netif_key`). Kütüphaneyi depo kökünden (`../..`)
kullanır.

Her istemci belirlenen aralıkla numaralı bir satır gönderir ve sunucunun onu
aynen geri gönderdiğini kontrol eder:

```
client2 #0 hello netif=WIFI_STA_DEF     her bağlantının ilk satırı
client2 #1
client2 #2
...
```

Dosyalar:

- `main/main.cpp`: istemci task'ları
- `main/client_config.h`: menuconfig ayarlarını bir tabloya çevirir
- `main/Kconfig.projbuild`: menuconfig ayarları
- `tools/tcp_multi_client_test.py`: bilgisayar için test sunucusu

## Ayarlar

`idf.py menuconfig`:

- **Example Connection Configuration**: Wi-Fi adı ve şifresi. Ethernet
  kullanmak için burada *Connect using Ethernet interface* seçeneğini de açın.
  *Preferred IPv6 Type* ayarını **Local Link** bırakın.
- **Multi Client Configuration**:
  - *Default server address* / *Default server port*: kendi adresi/portu boş /
    0 bırakılan her istemcinin kullandığı sunucu
  - *Message period*, *Connect timeout*, *Delay before reconnecting*
  - **Client 1 / Client 2 / Client 3**, her birinde:
    - *Enable client N*
    - *Server address* ve *Server port* (boş / 0 = varsayılan)
    - *Network interface*:

      | Seçenek | `netif_key` | |
      |---|---|---|
      | Any (yönlendirme karar verir) | `""` | istemci 1 ve 3 için varsayılan |
      | Wi-Fi station | `WIFI_STA_DEF` | istemci 2 için varsayılan |
      | Ethernet | `ETH_DEF` | yukarıda Ethernet'in açılmasını gerektirir |
      | Custom esp_netif key | yazdığınız anahtar | örneğin `WIFI_AP_DEF`, ya da kendi kodunuzun oluşturduğu bir anahtar |

Arayüzü olmayan bir istemci `interface "ETH_DEF" does not exist ...` yazıp
tekrar dener; diğer istemciler çalışmaya devam eder. IPv6 bir sunucu adresi
`tcplib → Enable IPv6 support` gerektirir.

Bir istemciyi bir arayüze bağlamak paketlerinin hangi ağdan çıkacağını
belirler; bu yüzden sunucu adresi o ağdan erişilebilir olmalıdır. Wi-Fi ve
Ethernet farklı ağlarsa, Ethernet istemcisine sunucunun Ethernet tarafındaki
adresini verin (kendi *Server address* ayarı); aksi hâlde bağlantı denemeleri
zaman aşımına uğrar.

## Wi-Fi + Ethernet (W5500) kurulumu

SPI2'ye bağlı W5500 modüllü bir ESP32'de test edildi. Bu ayarlar
`sdkconfig.w5500` dosyasında; yeni bir yapılandırmaya şöyle uygulanır:

```
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.w5500" build
```

(önce `sdkconfig` dosyasını silin; yalnızca dosya oluşturulurken uygulanır), ya
da elle ayarlayın: **Example Connection Configuration** içinde hem *Connect
using WiFi interface* hem *Connect using Ethernet interface* seçeneklerini açın,
sonra:

| Ayar | Değer |
|---|---|
| Ethernet Type | W5500 Module |
| SPI Host Number | 1 (SPI2) |
| SPI SCLK / MOSI / MISO / CS GPIO | 12 / 27 / 14 / 13 |
| Interrupt GPIO | 26 |
| PHY Reset GPIO | 25 |
| SPI clock speed (MHz) | 20 |

Notlar:

- Bu pinler SPI2'nin doğrudan bağlı (IOMUX) pinleri değil, GPIO matrisinden
  geçiyor; ESP32'de bu durumda okuma hızı yaklaşık 26 MHz ile sınırlı. Bu
  yüzden varsayılan 36 MHz yerine 20 MHz kullanılıyor.
- GPIO12 açılışta önemli bir pin (strapping, flash voltajını seçer). ESP32
  açılırken yüksek seviyeye çekilmemeli; aksi hâlde 3.3 V flash'lı modüller açılmaz.
- `example_connect()` açılan her arayüzün IP almasını bekler; bu yüzden Ethernet
  kablosu DHCP olan bir ağa takılı olmalıdır.
- W5500 IPv6 çoklu yayını süzer (açılışta `w5500.mac` uyarısı); IPv4 ve
  link-local IPv6 adresleme yine çalışır.

Bu kurulumla test sonucu (bilgisayar Wi-Fi'de 172.16.10.30, Ethernet'te
192.168.1.100; istemci 3 *Server address* = 192.168.1.100, arayüz Ethernet):

```
I client3: connected #1: 192.168.1.101 port 60760 -> 192.168.1.100 port 3333, interface ETH_DEF
I client2: connected #1: 172.16.10.65 port 60759 -> 172.16.10.30 port 3333, interface WIFI_STA_DEF
I client1: connected #1: 172.16.10.65 port 60758 -> 172.16.10.30 port 3333, interface WIFI_STA_DEF
```

```
python tools/tcp_multi_client_test.py --min-lines 10 --expect-netif 2=WIFI_STA_DEF --expect-netif 3=ETH_DEF
...
client1      1     10     0  any             172.16.10.65
client2      1     10     0  WIFI_STA_DEF    172.16.10.65
client3      1     10     0  ETH_DEF         192.168.1.101

PASS: clients 1, 2, 3 each sent >= 10 lines and got them echoed
```

Gecikmeyi azaltmak için `app_main()` içinde Wi-Fi güç tasarrufu kapatılıyor;
pille çalışan cihazlarda bu satırı kaldırın.

## Bilgisayarda test

`tools/tcp_multi_client_test.py` IPv4 ve IPv6'yı dinler, her satırı geri
gönderir ve istemcileri kontrol eder. Python 3.8+, yalnızca standart kütüphane.
*Default server address* ayarını bilgisayarın adresi yapın, kartı yükleyin, sonra:

```
python tools/tcp_multi_client_test.py
```

İstemci 1, 2 ve 3'ün her birinin numaralı satırları atlamadan en az 5 satır
göndermesini en fazla 60 saniye bekler; bir özet yazdırır ve 0 (GEÇTİ) ya da
1 (KALDI) çıkış koduyla biter:

```
client   conns  lines  gaps  netif           address
client1      1     10     0  any             172.16.10.65
client2      1     10     0  WIFI_STA_DEF    172.16.10.65
client3      1     10     0  WIFI_STA_DEF    172.16.10.65

PASS: clients 1, 2, 3 each sent >= 10 lines and got them echoed
```

Seçenekler:

| Seçenek | Anlamı |
|---|---|
| `--port 3333` | dinlenecek port |
| `--clients 1,2,3` | teste katılması gereken istemciler (örneğin istemci 3 kapalıysa `1,2`) |
| `--min-lines 5` | her istemcinin göndermesi gereken numaralı satır sayısı |
| `--duration 60` | bu kadar saniye sonra vazgeç |
| `--expect-netif 2=WIFI_STA_DEF` | bir istemcinin bu arayüzü bildirmesini iste (tekrarlanabilir) |
| `--serve` | yalnızca echo yap ve logla, sonsuza kadar, sonuç verme |
| `-v` | her satırı logla |

Test bitince bağlantıları kapatır; istemciler `read: Closed` yazıp yeniden
bağlanır, bu beklenen bir durumdur.

Windows Güvenlik Duvarı ilk seferde Python için izin isteyebilir; izin
verilmezse ESP32 bağlanamaz. İstemciler arasında çoklu yayını engelleyen Wi-Fi
ağlarında IPv6 için `../tcp_client/README.tr.md` dosyasına bakın.

## Derleme ve yükleme

```
idf.py set-target esp32
idf.py menuconfig
idf.py build flash monitor
```

`sdkconfig` depoda yoktur (Wi-Fi şifresini içerir); yeni bir yapılandırma
`sdkconfig.defaults` (yalnızca Wi-Fi) dosyasından başlar, Ethernet için yukarıda
gösterildiği gibi `sdkconfig.w5500` eklenir.
