# encode base64 the firmware.bin and upload it to the server

import base64
import requests

import firebase_admin
from firebase_admin import credentials
from firebase_admin import db
from firebase_admin import auth

# get database url and user email from secret.h
with open('include/secret.h') as f:
    for line in f:
        if '#define DATABASE_URL' in line:
            url = line.split('"')[1]
            print('Database URL: {}'.format(url))
        if '#define USER_EMAIL' in line:
            email = line.split('"')[1]
            print('User email: {}'.format(email))

# get the device name from main.cpp
with open('src/main.cpp') as f:
    for line in f:
        if '#define DEVICE_NAME' in line:
            device_name = line.split('"')[1]
            print('Device name: {}'.format(device_name))






if (url is None) or (email is None) or (device_name is None):
    print('Database URL or User email not found in secret.h')
    exit(1)

# authenticate
cred = credentials.Certificate('firebase-sdk.json')
firebase_admin.initialize_app(cred, {
    'databaseURL': url
})


# get the device path from the database
# device_path = user_id/device_id
user = auth.get_user_by_email(email)
ref = db.reference("/" + user.uid)
devices = ref.get()
for device_id in devices:
    print('Device ID: {}'.format(device_id))
    if 'info' not in devices[device_id]:
        continue
    if 'device' not in devices[device_id]['info']:
        continue
    if devices[device_id]['info']['device'] == device_name:
        device_path = "/" + user.uid + "/" + device_id
        print('Device path: {}'.format(device_path))
        break

if device_path is None:
    print('Device not found in the database')
    exit(1)


# get the firmware version
with open('include/version.h') as f:
    for line in f:
        if '#define DEVICE_VERSION' in line:
            version = line.split('"')[1]
            print('Firmware version: {}'.format(version))
        if '#define FIRMWARE_VERSION' in line:
            build_no = line.split(' ')[-1]
            print('Build number: {}'.format(build_no))








# encode the firmware.bin file
with open('.pio/build/nodemcu-32s/firmware.bin', 'rb') as file:
    firmware = base64.b64encode(file.read()).decode('utf-8')
    print('Firmware size: {} bytes'.format(len(firmware)))

# upload the firmware to the server
_fw_path = device_path + "/firmware/bin"
_fwver_path = device_path + "/firmware/version"
print('Firmware uploaded to {}'.format(_fw_path))
ref = db.reference(_fwver_path)
ref.set(int(build_no))
ref = db.reference(_fw_path)
ref.set(firmware)
print('Firmware uploaded to the server')




# send ota request to the device
ref = db.reference(device_path + "/data/restart")
ref.set("ota")
print('OTA request sent to the device')