#!/bin/bash
sudo ./scripts/rpc.py bdev_malloc_create -b data_bdev 64 4096
sudo ./scripts/rpc.py bdev_malloc_create -b metadata_bdev 64 4096
sudo ./scripts/rpc.py bdev_malloc_create -b back_bdev 64 4096
sudo ./scripts/rpc.py bdev_lvol_create_lvstore -bd back_bdev -md metadata_bdev data_bdev lvs0
sudo ./scripts/rpc.py bdev_lvol_create -l lvs0  lvol0 4
#sudo ./scripts/rpc.py bdev_get_bdevs > before_load.txt

echo "Taking snapshot"
sudo ./scripts/rpc.py bdev_lvol_snapshot lvs0/lvol0 snap1

echo "Cloning"
sudo ./scripts/rpc.py bdev_lvol_clone lvs0/snap1 clone1

#sudo ./scripts/rpc.py bdev_get_bdevs > after_clone.txt
sudo ./scripts/rpc.py bdev_get_bdevs -b lvs0/lvol0  > lvol0__a.txt
sudo ./scripts/rpc.py bdev_get_bdevs -b lvs0/snap1  > snap1__a.txt
sudo ./scripts/rpc.py bdev_get_bdevs -b lvs0/clone1 > clone1_a.txt

echo "Unloading"
sudo ./scripts/rpc.py bdev_lvol_unload_lvstore lvs0
echo "Reloading"
sudo ./scripts/rpc.py bdev_lvol_load_lvstore data_bdev -md metadata_bdev -bd back_bdev

#sudo ./scripts/rpc.py bdev_get_bdevs > after_load.txt
sudo ./scripts/rpc.py bdev_get_bdevs -b lvs0/lvol0  > lvol0__b.txt
sudo ./scripts/rpc.py bdev_get_bdevs -b lvs0/snap1  > snap1__b.txt
sudo ./scripts/rpc.py bdev_get_bdevs -b lvs0/clone1 > clone1_b.txt

#meld before_load.txt after_clone.txt after_load.txt &
meld lvol0__a.txt lvol0__b.txt &
meld snap1__a.txt snap1__b.txt &
meld clone1_a.txt clone1_b.txt &


