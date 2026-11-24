# esp32-tcplib

[English](README.md) | **Türkçe**

`tcplib`: **ESP-IDF** (>= 5.0) için lwIP TCP soketlerini saran ince bir C++ kütüphanesi.

Tek bir işi yapar: soket açar, veri taşır, ne olduğunu bildirir. Task, callback
ya da protokol içermez; bunları uygulamanızın ihtiyacına göre üstüne kurarsınız.

| Tür | Ne işe yarar |
|---|---|
| `TcpClient` | tek bir TCP bağlantısı: `connect`, `read`, `write`, `close` |
| `TcpListener` | dinleyen (sunucu) soket: `listen`, `accept` (`TcpClient` verir) |
| `TcpOptions` | soket açılırken uygulanan seçenekler; kullanılacak ağ arayüzü dahil |
| `tcp_status_t` | her işlemin sonucu: `Ok`, `Timeout`, `Closed`, `Error` |

- Bekleyen her çağrı bir zaman aşımı alır (`> 0` bekle, `0` bir kez dene, `< 0` sonsuza kadar bekle)
- Yalnızca taşınabilen (move-only) RAII nesneleri: yıkıcı soketi kapatır
- İstemciler ve dinleyiciler tek bir ağ arayüzüne (Wi-Fi, Ethernet, ...) bağlanabilir
- İsteğe bağlı IPv6 (IPv4 ve IPv6'yı birlikte dinleyen sunucular)
- Dosya tanımlayıcısı dışarı verilmez; kimse soketi arka planda bloklayan moda alamaz
- Log yok, global durum yok, namespace yok (her şey `Tcp` / `tcp` ön ekiyle başlar)

ESP32 üzerinde, ESP-IDF 5.5 ile, Wi-Fi ve W5500 SPI Ethernet üzerinden test edildi.

## Kurulum

### IDF component manager ile (önerilen)

Bileşeninizin `idf_component.yml` dosyasına (örneğin `main/idf_component.yml`):

```yaml
dependencies:
  tcplib:
    git: https://github.com/nsrtylmz/esp32-tcplib.git
    version: v0.1.0        # bir sürüm etiketi (tag)
```

### Düz bileşen olarak

ESP-IDF bir bileşene klasörünün adını verir; bu yüzden kütüphaneyi **`tcplib`**
adlı bir klasöre koyun:

```
git clone https://github.com/nsrtylmz/esp32-tcplib.git components/tcplib
```

(ya da `git submodule add https://github.com/nsrtylmz/esp32-tcplib.git components/tcplib`)

### Sonra

```cmake
idf_component_register(SRCS "main.cpp"
                       PRIV_REQUIRES tcplib)
```

```cpp
#include "tcplib.h"     // ya da tek tek: "TcpClient.h", "TcpListener.h", ...
```

Klasör yapısı:

```
esp32-tcplib/
  include/tcplib.h            toplu başlık dosyası
  include/*.h                 genel (public) başlıklar
  src/                        kaynak kod ve iç başlıklar
  Kconfig                     menuconfig → tcplib
  idf_component.yml           component manager tanımı
  Doxyfile                    API dokümanı (`doxygen` çalıştırın)
  examples/tcp_client/        istemci örneği (+ tools/tcp_echo_server.py)
  examples/tcp_multi_client/  her biri seçilen arayüzde üç istemci (+ tools/tcp_multi_client_test.py)
  examples/tcp_listener/      üç sunucu: yalnızca Wi-Fi, yalnızca Ethernet, ikisi de (+ tools/tcp_listener_test.py)
```

## Kullanım

### İstemci

```cpp
TcpClient client;
int err = 0;
tcp_status_t st = client.connect("192.168.1.10", 5000, 3000, TcpOptions(), &err);
if (st == tcp_status_t::Ok) {
    client.write("hello", 5, 1000);

    char buf[128];
    size_t got = 0;
    st = client.read(buf, sizeof(buf), got, 5000);
    if (st == tcp_status_t::Ok)           { /* buf içinde got bayt var */ }
    else if (st == tcp_status_t::Timeout) { /* henüz veri yok, bağlantı sağlam */ }
    else                                  { /* Closed ya da Error: karşı taraf gitti */ }
}
```

Yeniden bağlanmak için aynı nesnede `client.connect(...)` çağırın (önceki
bağlantı önce kapatılır).

`connect()` iptal edilemez: başka bir task'tan çağrılan `close()` onu
uyandırmaz. Negatif zaman aşımıyla el sıkışma başarılı ya da başarısız olana
kadar bekler; sonlu bir süre verip döngüde tekrar denemek daha doğrudur.

### Sunucu

```cpp
TcpListener listener;
listener.listen(5000, /*backlog=*/4);

TcpClient client;
if (listener.accept(client, 1000) == tcp_status_t::Ok) {
    // 'client' normal bir TcpClient: bir task'a, listeye, ... verin
}
```

`accept(out, 0)` beklemez; `accept(out, -1)` biri bağlanana ya da başka bir
task `close()` çağırana kadar bekler.

### Ağ arayüzünü seçmek

```cpp
TcpOptions o;
o.netif_key = "ETH_DEF";            // esp_netif anahtarı: "WIFI_STA_DEF", "WIFI_AP_DEF", "ETH_DEF", ...
client.connect("192.168.1.100", 5000, 3000, o);   // yalnızca Ethernet'ten çıkar
listener.listen(5000, 4, o);                       // yalnızca Ethernet bağlantılarını kabul eder
```

Bir arayüze bağlanan istemci paketlerini o arayüzden gönderir; bu yüzden sunucu
o ağdan erişilebilir olmalıdır. Bir arayüze bağlanan dinleyici yalnızca o
arayüzden gelen bağlantıları görür, diğerleri reddedilir. `TcpClient::netif()`
bir bağlantının hangi arayüzde olduğunu söyler. Bilinmeyen bir anahtar
`connect()` / `listen()` çağrısının `ENODEV` ile başarısız olmasına yol açar.

## Sonuç kodları

| tcp_status_t | Anlamı |
|---|---|
| `Ok` | Tamamlandı. `read` için: `got > 0`. |
| `Timeout` | Süre içinde bir şey olmadı. Bağlantı hâlâ sağlam. |
| `Closed` | Karşı taraf bağlantıyı kapattı (ya da sıfırladı), `close()` çağrıldı ya da daha önce kalıcı bir hata oldu. Bir sonraki `connect()`'e kadar kalıcıdır. |
| `Error` | Diğer her şey. `lastErrno()` errno değerini verir. |

`read()` önce tampondaki tüm veriyi teslim eder, `Closed`'u sonra bildirir.
`write()` ya hepsini gönderir ya da başarısız olur; `*written` ne kadarının
kabul edildiğini söyler (`Timeout` sonrasında işe yarar). `tcpStatusToString()`
yazdırılabilir bir ad verir.

`connect()` ve `listen()` hata kodunu isteğe bağlı `err_out` parametresiyle
bildirir: bir errno değeri, ya da ad çözümleme başarısız olduysa bir
`getaddrinfo()` kodu (`EAI_...`).

## Kopmuş bağlantıyı fark etmek

- `isOpen()`: yalnızca yerel durum, ağ işlemi yapmaz.
- `checkAlive()`: sokete göz atar. Bağlantıyı kapatan ya da sıfırlayan karşı tarafı fark eder.
- Sessizce kopan bir bağlantıyı (kablo çekildi, elektrik gitti) hiçbir soket
  çağrısı fark edemez. Bunu keepalive (varsayılan olarak açık; varsayılan
  `TcpOptions` ile ~45 sn) ya da uygulamanızın kendi heartbeat mesajı yakalar.

## Seçenekler

```cpp
TcpOptions o;
o.no_delay    = true;
o.keepalive   = true;  o.keep_idle_s = 30;  o.keep_interval_s = 5;  o.keep_count = 3;
o.netif_key   = "WIFI_STA_DEF";   // tek bir arayüze bağla, boş = herhangi biri
client.connect(host, port, 3000, o);
```

`TcpListener::listen()`'a verilen seçenekler kabul edilen her bağlantıya da
uygulanır. Diğer her şey için: `setOption(level, opt, val, len)` /
`getOption(...)` (sabitler için `lwip/sockets.h` dosyasını dahil edin). Bazı
lwIP seçenekleri yalnızca `menuconfig`'de açıldığında çalışır (örneğin
`SO_RCVBUF` için `CONFIG_LWIP_SO_RCVBUF` gerekir). `TcpOptions` içindeki TCP
seçenekleri "elden geldiğince" uygulanır; önemliyse `getOption()` ile geri okuyun.

## IPv6

Varsayılan olarak kapalı. `idf.py menuconfig` → `tcplib` → "Enable IPv6 support"
ile açın (`CONFIG_TCPLIB_IPV6`, `CONFIG_LWIP_IPV6` gerektirir). Sonra:

- `connect()` IPv6 adreslerini de kabul eder (`"2001:db8::10"`, `%zone` eki
  olmadan); host adlarında IPv4 tercih edilir.
- Link-local adresler (`fe80::...`) `TcpOptions::netif_key` arayüzünden, boşsa
  varsayılan arayüzden gönderilir (aksi hâlde lwIP'nin onlar için bir yolu yoktur).
- `TcpListener`, IPv4 ve IPv6 istemcileri birlikte kabul eden tek bir soket açar.
- `remoteAddr()` / `localAddr()` `esp_ip_addr_t` (IPv4 ya da IPv6) döndürür;
  `...IpString()` fonksiyonları ikisini de yazdırır. `remoteIp()` / `localIp()`
  yalnızca IPv4'tür ve IPv6 adreste `0.0.0.0` döndürür.

Link-local IPv6, iki cihaz arasında çalışan bir komşu keşfi (çoklu yayın)
gerektirir. Bazı Wi-Fi erişim noktaları bunu istemciler arasında iletmez; o
zaman ping bir yönde çalışsa bile bağlantılar zaman aşımına uğrar.
`examples/tcp_client/README.tr.md` dosyasına bakın.

## Task kuralları

| Çağrı | Kural |
|---|---|
| `write`, `shutdownWrite`, `waitWritable` | her task'tan; çağrılar sıraya girer, eşzamanlı yazmalar birbirine karışmaz |
| `read`, `waitReadable` | aynı anda tek task |
| `TcpListener::accept` | aynı anda tek task |
| `close` | her task'tan; diğer task'lardaki bekleyen çağrılar ~100 ms içinde `Closed` döner (`connect()` hariç, yukarıya bakın) |

Başka bir task kullanırken bir nesnede `connect()`, `listen()` çağırmayın; nesneyi taşımayın ya da yok etmeyin.

## Notlar ve sınırlar

- `CONFIG_TCPLIB_IPV6` açılmadıkça yalnızca IPv4 (yukarıya bakın). TLS yok.
- `connect()` zaman aşımı DNS çözümlemesini değil, yalnızca TCP el sıkışmasını
  kapsar; `connect()` iptal edilemez.
- Aynı anda açık soket sayısı: `CONFIG_LWIP_MAX_SOCKETS` (varsayılan 10).
- `TcpClient::netif()` `esp_netif_next_unsafe()` kullanır (ESP-IDF >= 5.0).
- Bekleyen çağrılar 100 ms'lik dilimlerle bekler (`close()` onları bu sayede kesebilir).

## Örnekler

| Örnek | Ne gösterir | Bilgisayar aracı |
|---|---|---|
| [tcp_client](examples/tcp_client) | tek istemci, yeniden bağlanma, seri monitörden yazılan satırları gönderme, IPv4/IPv6 | `tools/tcp_echo_server.py` |
| [tcp_multi_client](examples/tcp_multi_client) | aynı anda üç istemci, her biri seçilen arayüzde | `tools/tcp_multi_client_test.py` (GEÇTİ/KALDI testi) |
| [tcp_listener](examples/tcp_listener) | üç sunucu: yalnızca Wi-Fi, yalnızca Ethernet, ikisi de; gelen arayüzü bildirir | `tools/tcp_listener_test.py` (GEÇTİ/KALDI testi) |

Her örnek doğrudan depodan derlenir (kütüphaneyi iki üst klasörde bulur); depo
klasörünün adı ne olursa olsun:

```
cd examples/tcp_listener
idf.py set-target esp32
idf.py menuconfig        # Wi-Fi adı/şifresi, örnek ayarları
idf.py build flash monitor
```

`sdkconfig` depoda yoktur (Wi-Fi şifrenizi içerir); her örnek
`sdkconfig.defaults` dosyasından başlar. Çoklu istemci ve sunucu örneklerinde,
test edildikleri W5500 Ethernet ayarlarını içeren bir `sdkconfig.w5500` dosyası
da vardır.

## Test

Örneklerdeki Python araçları gerçek bir kartla çalışan uçtan uca testlerdir
(Python 3.8+, yalnızca standart kütüphane). Bir tablo yazdırıp 0 (GEÇTİ) ya da
1 (KALDI) çıkış koduyla biterler; bu yüzden betiklerde kullanılabilirler:

```
python examples/tcp_multi_client/tools/tcp_multi_client_test.py --expect-netif 2=WIFI_STA_DEF
python examples/tcp_listener/tools/tcp_listener_test.py --wifi 172.16.10.65 --eth 192.168.1.101
```

Seçenekler ve ESP32 + W5500 üzerindeki sonuçlar için her örneğin README'sine bakın.

## API dokümanı

Genel başlık dosyaları Doxygen yorumları içerir. [Doxygen](https://www.doxygen.nl)
kuruluysa bu klasörde `doxygen` çalıştırın ve `docs/html/index.html` dosyasını açın.

## Lisans

[MIT](LICENSE)
