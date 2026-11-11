(function () {
  const cfg = {
    name: "BacaIn",
    repo: "https://github.com/Kha-dr/BacaIn",
    repoSlug: "Kha-dr/BacaIn",
    appUrl: "https://bacain.khadr.dev",
    ble: {
      service: "0000fe60-0000-1000-8000-00805f9b34fb",
      control: "0000fe61-0000-1000-8000-00805f9b34fb",
      data: "0000fe62-0000-1000-8000-00805f9b34fb",
      status: "0000fe63-0000-1000-8000-00805f9b34fb",
      chunkSize: 506,
      chunkDelayMs: 6,
      opcodes: {
        LIST_BOOKS: 0x01,
        START_UPLOAD: 0x02,
        FINISH_UPLOAD: 0x03,
        DELETE_BOOK: 0x04,
        ABORT_UPLOAD: 0x05,
        SET_STORAGE_TARGET: 0x06,
        GET_BOOKMARKS: 0x07,
        DELETE_BOOKMARK: 0x08,
        ADD_BOOKMARK: 0x09,
        SET_BOOK_OFFSET: 0x0a,
        SET_FONT: 0x10,
        FORCE_REFRESH: 0x12,
        SET_SLEEP_TIMEOUT: 0x13,
        TRIGGER_SLEEP: 0x14,
        SET_NAV_HINTS: 0x15,
        SET_ROTATION: 0x16,
        SET_BUTTON_INVERT: 0x17,
        SET_STATUS_LED: 0x18,
        GET_CONFIG: 0x19,
        SET_REFRESH_INTERVAL: 0x1a,
        GET_MUSHAF_STATE: 0x20,
        SET_MUSHAF_STATE: 0x21,
        CAP_MUSHAF: 0x01,
        CAP_REFRESH_INTERVAL: 0x02,
      },
    },
    storage: {
      flash: 1,
      sd: 2,
      paths: {
        1: "/spiflash",
        2: "/sdcard",
      },
    },
    flasher: {
      baud: 460800,
      offset: "0x0",
    },
  };

  if (typeof window !== "undefined") {
    window.CONFIG = cfg;
    window.BACA_CONFIG = cfg;
  }
  if (typeof module !== "undefined") {
    module.exports = { CONFIG: cfg };
  }
})();
