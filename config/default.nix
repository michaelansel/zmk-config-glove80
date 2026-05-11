{ firmware ? import ../src {} }:

let
  config = ./.;

in {
  left = firmware.zmk.override {
    board   = "glove80_lh";
    keymap  = "${config}/glove80.keymap";
    kconfig = "${config}/glove80_lh_peripheral.conf";
  };

  right = firmware.zmk.override {
    board   = "glove80_rh";
    keymap  = "${config}/glove80.keymap";
    kconfig = "${config}/glove80_rh_peripheral.conf";
  };

  dongle =
    let base = firmware.zmk.override {
      board        = "seeeduino_xiao_ble";
      shield       = "glove80_dongle prospector_adapter";
      keymap       = "${config}/glove80.keymap";
      kconfig      = "${config}/glove80_dongle.conf";
      extraModules = [ "${config}" "${config}/modules/prospector-zmk-module" ];
    };
    # The moergo ZMK fork's app/boards/seeeduino_xiao_ble.overlay is found
    # twice — once via BOARD_ROOT=. and once via APPLICATION_SOURCE_DIR — because
    # both resolve to the same source/app/ directory.  Remove it before CMake
    # runs; the QSPI flash and serial config it contained are re-applied via
    # the glove80_dongle shield overlay instead.
    in base.overrideAttrs (old: {
      preConfigure = (old.preConfigure or "") + "\nrm -f boards/seeeduino_xiao_ble.overlay";
    });
}
