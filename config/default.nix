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

  dongle = firmware.zmk.override {
    board        = "nice_nano_v2";
    shield       = "glove80_dongle";
    keymap       = "${config}/glove80.keymap";
    kconfig      = "${config}/glove80_dongle.conf";
    extraModules = [ "${config}" ];
  };
}
