FROM espressif/idf:v5.3.1

WORKDIR /project/firmware

# Default command: configure ESP32-S3 target, build firmware, and package artifacts for Web Flasher
ENTRYPOINT ["/opt/esp/entrypoint.sh"]
CMD ["bash", "-c", "idf.py set-target esp32s3 && idf.py build && python3 package_firmware.py --web-dir ../web/firmware"]
