
CC = gcc
CFLAGS = -O6 -Wall -Wpedantic -Wextra
LDFLAGS = -lmosquitto
TARGET = rtsptomqtt

##

$(TARGET): $(TARGET).c include/config_linux.h include/mqtt_linux.h include/exec_linux.h
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c $(LDFLAGS)
all: $(TARGET)
clean:
	rm -f $(TARGET)
format:
	clang-format -i $(TARGET).c include/*.h
test: $(TARGET)
	./$(TARGET) --config $(TARGET).cfg
.PHONY: all clean format test

##

SYSTEMD_DIR = /etc/systemd/system
define install_systemd_service
	-systemctl stop $(1) 2>/dev/null || true
	-systemctl disable $(1) 2>/dev/null || true
	cp $(2).service $(SYSTEMD_DIR)/$(1).service
	systemctl daemon-reload
	systemctl enable $(1)
	systemctl start $(1) || echo "Warning: Failed to start $(1)"
endef
install_systemd_service: $(TARGET).service
	$(call install_systemd_service,$(TARGET),$(TARGET))
install: install_systemd_service
restart:
	systemctl restart $(TARGET)
.PHONY: install install_systemd_service

