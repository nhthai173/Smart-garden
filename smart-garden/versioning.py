FILENAME_BUILDNO = 'versioning'
FILENAME_VERSION_H = 'include/version.h'
version = '0.3.'

build_no = 0
try:
    with open(FILENAME_BUILDNO) as f:
        build_no = int(f.readline()) + 1
except:
    print('Starting build number from 1..')
    build_no = 1
with open(FILENAME_BUILDNO, 'w+') as f:
    f.write(str(build_no))
    print('Build number: {}'.format(build_no))

hf = """
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION {}
#endif
#ifndef DEVICE_VERSION
  #define DEVICE_VERSION "{}"
#endif
""".format(build_no, version+str(build_no))
with open(FILENAME_VERSION_H, 'w+') as f:
    f.write(hf)