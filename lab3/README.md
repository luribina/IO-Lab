# Лабораторная работа 3

**Название:** Разработка драйверов сетевых устройств"

**Цель работы:** Получить знания и навыки разработки драйверов сетевых
интерфейсов для операционной системы Linux.

## Описание функциональности драйвера

Пакеты протокола IPv4, адресуемые конкретному IP. Вывести IP
адреса отправителя и получателя.
Состояние разбора пакетов необходимо выводить в файл в
директории /proc

## Инструкция по сборке

1. Собрать драйвер `make`
2. Загрузить драйвер в ядро `insmod lab3.ko`
3. Удалить драйвер `rmmod lab3`
4. Очистить файлы, созданные при сборке `make clean`

## Инструкция пользователя

Доступные действия :

1. При загрузке драйвера можно указать такие параметры как
    1. `link` - интерфейс, пакеты которого необходимо перехватывать
    2. `dest` - адресуемый IP, который необходимо отслеживать

2. `cat /proc/var2` - вывести адреса отправителя и получателя

3. `dmesg` - вывести буфер ядра, в который записываются служебные сообщения драйвера

4. `ip addr show <device>` - вывести адрес конкретного устройства

5. `ip -s link show <device>` - вывести статистику для устройства

6. `ping <address>` - передача пакетов на адрес

## Примеры использования

1. `sudo insmod lab3.ko dest=127.0.0.14`

2. `dmesg`
```
[ 1278.082778] Init addr: 127.0.0.14
[ 1278.082790] Module lab3 loaded
[ 1278.082791] lab3: create link vni0
[ 1278.082792] lab3: registered rx handler for lo
```

3. `ip addr show vni0`
```
anna@anna-VirtualBox:~/uni/io/IO-Lab/lab3$ ip addr show vni0
4: vni0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UNKNOWN group default qlen 1000
    link/ether 00:00:00:00:00:00 brd 00:00:00:00:00:00
```

4. `ping 127.0.0.14`
```
anna@anna-VirtualBox:~/uni/io/IO-Lab/lab3$ ping 127.0.0.14
PING 127.0.0.14 (127.0.0.14) 56(84) bytes of data.
64 bytes from 127.0.0.14: icmp_seq=1 ttl=64 time=0.034 ms
64 bytes from 127.0.0.14: icmp_seq=2 ttl=64 time=0.096 ms
64 bytes from 127.0.0.14: icmp_seq=3 ttl=64 time=0.037 ms
64 bytes from 127.0.0.14: icmp_seq=4 ttl=64 time=0.097 ms
64 bytes from 127.0.0.14: icmp_seq=5 ttl=64 time=0.116 ms
^C
--- 127.0.0.14 ping statistics ---
5 packets transmitted, 5 received, 0% packet loss, time 4080ms
rtt min/avg/max/mdev = 0.034/0.076/0.116/0.033 ms
```
5. `cat /proc/var2`
```
anna@anna-VirtualBox:~/uni/io/IO-Lab/lab3$ cat /proc/var2 
saddr: 127.0.0.1
daddr: 127.0.0.14
saddr: 127.0.0.1
daddr: 127.0.0.14
saddr: 127.0.0.1
daddr: 127.0.0.14
saddr: 127.0.0.1
daddr: 127.0.0.14
saddr: 127.0.0.1
daddr: 127.0.0.14
```

6. `ip -s link show vni0`
```
anna@anna-VirtualBox:~/uni/io/IO-Lab/lab3$ ip -s link show vni0
4: vni0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel state UNKNOWN mode DEFAULT group default qlen 1000
    link/ether 00:00:00:00:00:00 brd 00:00:00:00:00:00
    RX: bytes  packets  errors  dropped overrun mcast   
    420        5        0       0       0       0       
    TX: bytes  packets  errors  dropped carrier collsns 
    0          0        0       0       0       0  
```
