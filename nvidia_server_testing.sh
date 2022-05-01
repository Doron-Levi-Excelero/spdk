#!/bin/bash
#on n245:
#attach md raid10:
MD_VOLUME=nvmfvolume
DATA_VOLUME=vol2
BDEV_SUFFIX=_bdev_nvmeibc

MD_BDEV=$MD_VOLUME$BDEV_SUFFIX
DATA_BDEV=$DATA_VOLUME$BDEV_SUFFIX

LVOL_STORE_NAME=lvs0
LVOL_NAME=lvol0
SNAPSHOT_NAME=snap1
CLONE_NAME=clone1

sudo nvmesh_attach_volumes $MD_VOLUME
#attach data ec:
sudo nvmesh_attach_volumes $DATA_VOLUME
#Create aio bdevs:
echo "Create aio bdevs"
sudo ./scripts/rpc.py bdev_aio_create /dev/nvmesh/$MD_VOLUME $MD_BDEV
sudo ./scripts/rpc.py bdev_aio_create /dev/nvmesh/$DATA_VOLUME $DATA_BDEV

echo "Create lvol store"
sudo ./scripts/rpc.py bdev_lvol_create_lvstore -md $MD_BDEV $DATA_BDEV $LVOL_STORE_NAME
echo "Create lvol"
sudo ./scripts/rpc.py bdev_lvol_create -l $LVOL_STORE_NAME  $LVOL_NAME 4

echo "Taking snapshot"
sudo ./scripts/rpc.py bdev_lvol_snapshot $LVOL_STORE_NAME/$LVOL_NAME $SNAPSHOT_NAME

echo "Cloning"
sudo ./scripts/rpc.py bdev_lvol_clone $LVOL_STORE_NAME/$SNAPSHOT_NAME $CLONE_NAME

echo "Unloading"
sudo ./scripts/rpc.py bdev_lvol_unload_lvstore $LVOL_STORE_NAME
echo "Reloading"
sudo ./scripts/rpc.py bdev_lvol_load_lvstore $DATA_BDEV -md $MD_BDEV
