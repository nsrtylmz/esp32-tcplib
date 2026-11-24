tcp_client örneği
=================

[English](README.md) | **Türkçe**

Bir TCP sunucusuna bağlanır, ilk mesajı gönderir, gelen her şeyi loglar ve
bağlantı koparsa yeniden bağlanır. Seri monitöre yazılan satırlar sunucuya
gönderilir (bağlantı yokken yazılanlar sıraya alınır, en fazla 8 satır).
Kütüphaneyi depo kökünden (`../..`) kullanır.

Dosyalar:

- `main/main.cpp`: bağlantı döngüsü
- `main/console.cpp`: seri konsoldan satır okur ve sıraya koyar; `TcpClient`'a
  yalnızca ana task dokunur
- `tools/tcp_echo_server.py`: bilgisayar için test sunucusu

## Ayarlar

`idf.py menuconfig`:

- **Example Connection Configuration**: Wi-Fi adı ve şifresi. *Preferred IPv6
  Type* ayarını, ağınız başka türde IPv6 adresi dağıtmıyorsa **Local Link**
  bırakın (`example_connect()` o türde bir adres gelene kadar bekler).
- **Example Configuration**:
  - *IP Version*: IPV4 ya da IPV6 (IPV6, `tcplib → Enable IPv6 support`
    seçeneğini de açar)
  - *IPV4 Address* / *IPV6 Address*: sunucu (bilgisayarınız)
  - *Port*: sunucu portu (varsayılan 3333)
  - *Connect timeout (ms)*: tek bir bağlantı denemesinin süresi (varsayılan 10000)
  - *Interface selection*: Auto, ya da `WIFI_STA_DEF` gibi bir esp_netif
    anahtarı (`TcpOptions::netif_key` olarak kullanılır)

Gecikmeyi azaltmak için `app_main()` içinde Wi-Fi güç tasarrufu kapatılıyor
(`esp_wifi_set_ps(WIFI_PS_NONE)`); pille çalışan cihazlarda bu satırı kaldırın.

## Seri konsoldan mesaj gönderme

`idf.py monitor` içinde bir satır yazıp Enter'a basın: satır sunucuya yazıldığı
gibi gönderilir. Sonuna `\n` eklenmez; kontrol karakterleri (Ctrl tuşları,
escape dizileri) çıkarılır. Monitörden `Ctrl+]` ile ya da önce `Ctrl+T` sonra
`Ctrl+X` ile çıkılır (`]` için AltGr gereken klavyelerde, örneğin Türkçe Q
klavyede, ikincisi daha kolaydır).

**Not:** konsoldan giriş yalnızca konsol bir UART olduğunda çalışır
(`CONFIG_ESP_CONSOLE_UART`; CP210x ya da CH340 gibi USB-UART çipi olan ESP32
kartlarında varsayılan budur). Çipin dahili USB portunu konsol olarak kullanan
kartlar (USB-Serial-JTAG ya da USB-CDC, örneğin birçok ESP32-C3/S3 kartı) farklı
bir sürücü gerektirir; onlarda `console.cpp` hiçbir şey kurmaz ve giriş gelmez.

## Bilgisayarda test sunucusu

`tools/tcp_echo_server.py`, IPv4 ve IPv6'yı aynı anda dinleyen küçük bir echo
sunucusudur. Python 3.8+ yeterli, ek paket gerekmez:

```
python tools/tcp_echo_server.py 3333
```

Bilgisayardaki birçok araç (örneğin Hercules) yalnızca IPv4 dinler; ESP32 onlara
IPv6 ile ulaşamaz. `netstat -an` ile kontrol edin: IPv6 dinleyen bir soket
`[::]:3333` olarak görünür.

Bilgisayarın IPv6 adresini `ipconfig` (Windows) ya da `ip -6 addr` (Linux) ile
bulun. Link-local bir adres (`fe80::...`) aynı Wi-Fi ağında çalışır; Windows'un
gösterdiği `%<sayı>` ekini yazmayın. Windows Güvenlik Duvarı ilk seferde Python
için izin isteyebilir; izin verilmezse gelen bağlantılar engellenir.

Bilgisayar ESP32'ye ping atabildiği hâlde IPv6 bağlantıları zaman aşımına
uğruyorsa, erişim noktası istemciler arasında IPv6 çoklu yayın (komşu keşfi)
paketlerini iletmiyor olabilir. O ağda IPv4 kullanın ya da bilgisayarın mobil
etkin noktası (hotspot) üzerinden deneyin (sunucu adresi o zaman hotspot
bağdaştırıcısının `fe80::...` adresidir).

## Derleme ve yükleme

```
idf.py set-target esp32
idf.py menuconfig
idf.py build flash monitor
```

`sdkconfig` depoda yoktur (Wi-Fi şifresini içerir); yeni bir yapılandırma
`sdkconfig.defaults` dosyasından başlar.
