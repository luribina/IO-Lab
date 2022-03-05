# Лабораторная работа 1

**Название:** "Разработка драйверов символьных устройств"

**Цель работы:** получить знания и навыки разработки драйверов символьных
устройств для операционной системы Linux.

## Описание функциональности драйвера

При записи в файл символьного устройства текста типа “5+6”
должен запоминаться результат операции, то есть 11 для
данного примера. Должны поддерживаться операции сложения,
вычитания, умножения и деления. Последовательность
полученных результатов с момента загрузки модуля ядра
должна выводиться при чтении созданного файла /proc/varN в
консоль пользователя.
При чтении из файла символьного устройства в кольцевой
буфер ядра должен осуществляться вывод тех же данных,
которые выводятся при чтении файла /proc/varN.

## Инструкция по сборке

```
    $ make
```

## Инструкция пользователя

1. Собрать драйвер в соотвествии инструкции по сборке
2. Загрузить модуль с помощью
   ```
    # insmod lab1_dev.ko
   ```
3. С помощью `echo` записать какое-либо выражение в `/dev/lab1_dev`
4. Прочитать полученные результаты из файла `/proc/var2` или вывести их в буфер ядра с помощью чтения `/dev/lab1_dev`
5. Выгрузить модуль с помощью
   ```
    # rmmod lab1_dev
   ```
6. Очистить файлы при сборке
   ```
    $ make clean
   ```

## Примеры использования

```
anna@anna-ubuntu:~/uni/io/lab1$ sudo insmod lab1_dev.ko 
[sudo] password for anna: 
anna@anna-ubuntu:~/uni/io/lab1$ echo "2*(3+1)" > /dev/lab1_dev 
anna@anna-ubuntu:~/uni/io/lab1$ cat /proc/var2 
Calculated results:
8
anna@anna-ubuntu:~/uni/io/lab1$ cat /dev/lab1_dev 
anna@anna-ubuntu:~/uni/io/lab1$ sudo dmesg | tail -10
[  102.893893] audit: type=1326 audit(1646508110.156:49): auid=1000 uid=1000 gid=1000 ses=3 subj=snap.snap-store.ubuntu-software pid=2004 comm="pool-org.gnome." exe="/snap/snap-store/558/usr/bin/snap-store" sig=0 arch=c000003e syscall=93 compat=0 ip=0x7fbb09b604fb code=0x50000
[  272.537080] Loaded lab1 module
[  272.541009] Success!
[  294.001738] Write to dev
[  299.405248] Proc file read
[  299.405261] Proc file read
[  299.405262] All done
[  307.007032] Calculated results:
               8
anna@anna-ubuntu:~/uni/io/lab1$ sudo rmmod lab1_dev 
anna@anna-ubuntu:~/uni/io/lab1$ sudo dmesg | tail -10
[  272.537080] Loaded lab1 module
[  272.541009] Success!
[  294.001738] Write to dev
[  299.405248] Proc file read
[  299.405261] Proc file read
[  299.405262] All done
[  307.007032] Calculated results:
               8

[  333.169443] Unloaded lab1 module
```
