log_file=${1:-log.txt}
idf.py --preview set-target linux && idf.py build && ./build/test_touch_sensor_fsm.elf >> "$log_file"
