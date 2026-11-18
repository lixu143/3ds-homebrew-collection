# HTTP Share 3DS - sysmodule edition
# Build both CIAs: the resident sysmodule and its launcher.

.PHONY: all module launcher clean

all: module launcher

module:
	@$(MAKE) --no-print-directory -C module

launcher:
	@$(MAKE) --no-print-directory -C launcher

clean:
	@$(MAKE) --no-print-directory -C module clean
	@$(MAKE) --no-print-directory -C launcher clean

# Convenience: copy the finished CIAs to a folder of your choice, e.g.
#   make copy DEST=/e/3ds
copy:
	@mkdir -p $(DEST)
	@cp module/HTTPShare_Module.cia launcher/HTTPShare_Launcher.cia $(DEST)/
	@echo "copied 2 CIAs to $(DEST)"
