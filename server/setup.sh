#!/usr/bin/env sh

TUN="tun0"
NIC=$(ip link | aws -F: '$0 !~ "lo|vir|tun|^[^0-9]"{print $2;getline}')

sudo sh -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'
sudo ip link set ${TUN} up
sudo ipconfig ${TUN} 10.233.233.1/24
sudo iptables -t nat -A POSTROUTING -s 10.233.233.0/24 -o ${NIC} -j MASQUERADE
