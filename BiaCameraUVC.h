#ifndef BIA_CAMERA_UVC_H
#define BIA_CAMERA_UVC_H

#include <Arduino.h>
#include <WiFi.h>
#include "usb/usb_host.h"

// Registradores do controlador DWC_OTG (ESP32-S3)
#define BIA_USB_GAHBCFG_REG   (*(volatile uint32_t *)0x60080008)
#define BIA_USB_HAINTMSK_REG  (*(volatile uint32_t *)0x60080418)
#define BIA_USB_HPRT_REG      (*(volatile uint32_t *)0x60080440)

#define BIA_USB_HCCHAR0_REG   (*(volatile uint32_t *)0x60080500)
#define BIA_USB_HCINT0_REG    (*(volatile uint32_t *)0x60080508)
#define BIA_USB_HCTSIZ0_REG   (*(volatile uint32_t *)0x60080510)
#define BIA_USB_HCDMA0_REG    (*(volatile uint32_t *)0x60080514)

#define BIA_USB_HCCHAR1_REG   (*(volatile uint32_t *)0x60080520)
#define BIA_USB_HCINT1_REG    (*(volatile uint32_t *)0x60080528)
#define BIA_USB_HCTSIZ1_REG   (*(volatile uint32_t *)0x60080530)
#define BIA_USB_HCDMA1_REG    (*(volatile uint32_t *)0x60080534)

#define BIA_MAX_JPEG_SIZE     65536
#define BIA_HPRT_W1C_MASK     ((1UL << 1) | (1UL << 2) | (1UL << 3) | (1UL << 5))

static DMA_ATTR __attribute__((aligned(512))) uint32_t _bia_qtd_ctrl[8];
static DMA_ATTR __attribute__((aligned(512))) uint32_t _bia_qtd_video[8];
static DMA_ATTR __attribute__((aligned(64)))  uint8_t  _bia_buf_ctrl[512];
static DMA_ATTR __attribute__((aligned(64)))  uint8_t  _bia_buf_video[64];

class BiaCameraUVC {
public:
  // Inicia em modo inteligente: a camera so liga quando o Python estiver aberto e pedindo video!
  bool begin(const char *servidor_ip, uint16_t porta_tcp = 5005, uint8_t frame_index = 3) {
    if (_iniciado) return true;
    _ip = servidor_ip;
    _porta = porta_tcp;
    _frame_idx = frame_index;

    // Comeca desligada (Standby) ate o Python abrir e conectar!
    _camera_habilitada = false;
    _python_conectado = false;

    _jpeg_captura = (uint8_t *)(psramFound() ? ps_malloc(BIA_MAX_JPEG_SIZE) : malloc(BIA_MAX_JPEG_SIZE));
    _jpeg_pronto  = (uint8_t *)(psramFound() ? ps_malloc(BIA_MAX_JPEG_SIZE) : malloc(BIA_MAX_JPEG_SIZE));

    if (!_jpeg_captura || !_jpeg_pronto) {
      Serial.println("[BiaCamera] Erro ao alocar memoria para JPEG!");
      return false;
    }

    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    if (usb_host_install(&host_config) != ESP_OK) {
      Serial.println("[BiaCamera] Erro ao instalar USB Host!");
      return false;
    }

    xTaskCreatePinnedToCore(_taskUsbEvents, "bia_usb_evt", 4096, this, 20, &_usb_task_hdl, 0);
    xTaskCreatePinnedToCore(_taskEnvioTcp,  "bia_cam_tcp", 4096, this, 4,  &_tcp_task_hdl, 0);
    xTaskCreatePinnedToCore(_taskCaptura,   "bia_cam_cap", 4096, this, 5,  &_cap_task_hdl, 1);

    _iniciado = true;
    Serial.printf("[BiaCamera] Pronta em Standby! Aguardando o Python abrir em %s:%d...\n", _ip, _porta);
    return true;
  }

  // Permite forcar o estado pelo ESP32 caso queira, mas por padrao o Python controla sozinho
  void setEstado(bool ligada) {
    if (_camera_habilitada == ligada) return;
    _camera_habilitada = ligada;
    Serial.printf("[BiaCamera] Estado alterado para: %s\n", ligada ? "LIGADA" : "STANDBY");
  }

  void ligar()    { setEstado(true); }
  void desligar() { setEstado(false); }

  bool isLigada() const    { return _camera_habilitada && _python_conectado; }
  bool isStreaming() const { return _stream_ativo && _camera_habilitada; }

private:
  const char *_ip = NULL;
  uint16_t _porta = 5005;
  uint8_t _frame_idx = 3;
  bool _iniciado = false;

  volatile bool _camera_habilitada = false;
  volatile bool _python_conectado = false;
  volatile bool _stream_ativo = false;
  bool _barramento_suspenso = false;

  uint8_t *_jpeg_captura = NULL;
  uint8_t *_jpeg_pronto  = NULL;
  volatile uint32_t _tam_jpeg_pronto = 0;
  volatile uint32_t _id_quadro_pronto = 0;
  portMUX_TYPE _mux_frame = portMUX_INITIALIZER_UNLOCKED;

  TaskHandle_t _usb_task_hdl = NULL;
  TaskHandle_t _tcp_task_hdl = NULL;
  TaskHandle_t _cap_task_hdl = NULL;

  uint32_t _haintmsk_salvo = 0;
  uint32_t _gahbcfg_salvo  = 0;

  uint8_t _pid_ep81 = 0;
  uint32_t _pos_captura = 0;
  bool _dentro_do_jpeg = false;
  uint8_t _ultimo_byte = 0;

  uint8_t _uvc_hdr_len = 0;
  uint8_t _uvc_bfh_base = 0;
  uint32_t _uvc_max_payload = 0;
  uint32_t _bytes_no_payload = 0;

  inline bool _cameraPlugada() {
    return (BIA_USB_HPRT_REG & 0x01) != 0;
  }

  inline bool _deveCapturar() {
    return _python_conectado && _camera_habilitada;
  }

  void _suspenderFisicamenteUSB() {
    if (_barramento_suspenso) return;

    if (BIA_USB_HCCHAR0_REG & (1UL << 31)) { BIA_USB_HCCHAR0_REG |= (1UL << 30); vTaskDelay(pdMS_TO_TICKS(2)); }
    if (BIA_USB_HCCHAR1_REG & (1UL << 31)) { BIA_USB_HCCHAR1_REG |= (1UL << 30); vTaskDelay(pdMS_TO_TICKS(2)); }

    _dentro_do_jpeg = false;
    _pos_captura = 0;
    _stream_ativo = false;

    if (_cameraPlugada()) {
      uint32_t hprt = BIA_USB_HPRT_REG & ~BIA_HPRT_W1C_MASK;
      BIA_USB_HPRT_REG = hprt | (1UL << 7); // Ativa USB Suspend fisico
    }
    _barramento_suspenso = true;
    Serial.println("[BiaCamera] Camera em Standby USB (Economizando bateria).");
  }

  void _acordarFisicamenteUSB() {
    if (!_barramento_suspenso) return;

    if (_cameraPlugada() && (BIA_USB_HPRT_REG & (1UL << 7))) {
      uint32_t hprt = BIA_USB_HPRT_REG & ~BIA_HPRT_W1C_MASK;
      BIA_USB_HPRT_REG = hprt | (1UL << 6); // Resume K-state
      vTaskDelay(pdMS_TO_TICKS(25));
      hprt = BIA_USB_HPRT_REG & ~BIA_HPRT_W1C_MASK;
      BIA_USB_HPRT_REG = hprt & ~((1UL << 6) | (1UL << 7));
      vTaskDelay(pdMS_TO_TICKS(15));
    }
    _barramento_suspenso = false;
  }

  bool _etapaControle(uint8_t dev_addr, uint8_t ep_dir, uint8_t pid, uint32_t qtd_val, void *ptr, uint16_t timeout_ms) {
    if (!_cameraPlugada()) return false;
    _bia_qtd_ctrl[0] = qtd_val;
    _bia_qtd_ctrl[1] = (uint32_t)ptr;
    BIA_USB_HCINT0_REG  = 0xFFFFFFFF;
    BIA_USB_HCDMA0_REG  = (uint32_t)&_bia_qtd_ctrl[0];
    BIA_USB_HCTSIZ0_REG = ((uint32_t)pid << 29) | 0xFF;
    BIA_USB_HCCHAR0_REG = (1UL << 31) | ((uint32_t)dev_addr << 22) | ((uint32_t)ep_dir << 15) | 64;

    unsigned long t0 = millis();
    while (BIA_USB_HCCHAR0_REG & (1UL << 31)) {
      if (!_cameraPlugada() || (millis() - t0 > timeout_ms)) {
        BIA_USB_HCCHAR0_REG |= (1UL << 30);
        vTaskDelay(pdMS_TO_TICKS(2));
        break;
      }
    }
    uint32_t hcint = BIA_USB_HCINT0_REG;
    return ((hcint & 0x01) != 0) && ((hcint & 0x8C) == 0);
  }

  bool _controleOut(uint8_t dev_addr, uint8_t bmReq, uint8_t bReq, uint16_t wVal, uint16_t wIdx, uint16_t wLen, const uint8_t *dados) {
    _bia_buf_ctrl[0] = bmReq; _bia_buf_ctrl[1] = bReq;
    _bia_buf_ctrl[2] = wVal & 0xFF; _bia_buf_ctrl[3] = (wVal >> 8) & 0xFF;
    _bia_buf_ctrl[4] = wIdx & 0xFF; _bia_buf_ctrl[5] = (wIdx >> 8) & 0xFF;
    _bia_buf_ctrl[6] = wLen & 0xFF; _bia_buf_ctrl[7] = (wLen >> 8) & 0xFF;

    uint32_t q_setup = (1UL << 31) | (1UL << 26) | (1UL << 25) | (1UL << 24) | 8;
    if (!_etapaControle(dev_addr, 0, 3, q_setup, _bia_buf_ctrl, 250)) return false;

    if (wLen > 0 && dados != NULL) {
      memcpy(_bia_buf_ctrl + 64, dados, wLen);
      uint32_t q_data = (1UL << 31) | (1UL << 26) | (1UL << 25) | wLen;
      if (!_etapaControle(dev_addr, 0, 2, q_data, _bia_buf_ctrl + 64, 250)) return false;
    }

    uint32_t q_status = (1UL << 31) | (1UL << 26) | (1UL << 25) | 0;
    return _etapaControle(dev_addr, 1, 2, q_status, _bia_buf_ctrl + 128, 250);
  }

  int _controleIn(uint8_t dev_addr, uint8_t bmReq, uint8_t bReq, uint16_t wVal, uint16_t wIdx, uint16_t wLen, uint8_t *destino) {
    _bia_buf_ctrl[0] = bmReq; _bia_buf_ctrl[1] = bReq;
    _bia_buf_ctrl[2] = wVal & 0xFF; _bia_buf_ctrl[3] = (wVal >> 8) & 0xFF;
    _bia_buf_ctrl[4] = wIdx & 0xFF; _bia_buf_ctrl[5] = (wIdx >> 8) & 0xFF;
    _bia_buf_ctrl[6] = wLen & 0xFF; _bia_buf_ctrl[7] = (wLen >> 8) & 0xFF;

    uint32_t q_setup = (1UL << 31) | (1UL << 26) | (1UL << 25) | (1UL << 24) | 8;
    if (!_etapaControle(dev_addr, 0, 3, q_setup, _bia_buf_ctrl, 250)) return -1;

    uint16_t tam_dma = (wLen + 63) & ~63;
    memset(_bia_buf_ctrl + 64, 0, tam_dma);
    uint32_t q_data = (1UL << 31) | (1UL << 26) | (1UL << 25) | tam_dma;
    _etapaControle(dev_addr, 1, 2, q_data, _bia_buf_ctrl + 64, 250);

    int recebidos = tam_dma - (_bia_qtd_ctrl[0] & 0x1FFFF);
    if (recebidos > wLen) recebidos = wLen;

    uint32_t q_status = (1UL << 31) | (1UL << 26) | (1UL << 25) | 0;
    _etapaControle(dev_addr, 0, 2, q_status, _bia_buf_ctrl, 250);

    if (recebidos > 0) memcpy(destino, _bia_buf_ctrl + 64, recebidos);
    return recebidos;
  }

  bool _iniciarUVC(uint8_t dev_addr) {
    if (!_controleOut(dev_addr, 0x00, 0x09, 0x0001, 0x0000, 0, NULL)) return false;

    uint8_t uvc_ctrl[34] = {0};
    int tam_uvc = 26;
    int lidos = _controleIn(dev_addr, 0xA1, 0x81, 0x0100, 0x0001, 34, uvc_ctrl);
    if (lidos == 26 || lidos == 34) tam_uvc = lidos;

    memset(uvc_ctrl, 0, sizeof(uvc_ctrl));
    uvc_ctrl[0] = 0x01;
    uvc_ctrl[2] = 1; // MJPEG
    uvc_ctrl[3] = _frame_idx;
    uint32_t intervalo = 400000;
    memcpy(&uvc_ctrl[4], &intervalo, 4);

    _controleOut(dev_addr, 0x21, 0x01, 0x0100, 0x0001, tam_uvc, uvc_ctrl);

    uint8_t resp_probe[34] = {0};
    if (_controleIn(dev_addr, 0xA1, 0x81, 0x0100, 0x0001, tam_uvc, resp_probe) > 0) {
      _uvc_max_payload = resp_probe[22] | (resp_probe[23] << 8) | (resp_probe[24] << 16) | (resp_probe[25] << 24);
      memcpy(uvc_ctrl, resp_probe, tam_uvc);
    }
    uvc_ctrl[2] = 1;
    uvc_ctrl[3] = _frame_idx;

    if (!_controleOut(dev_addr, 0x21, 0x01, 0x0200, 0x0001, tam_uvc, uvc_ctrl)) return false;

    _pid_ep81 = 0;
    _dentro_do_jpeg = false;
    _pos_captura = 0;
    _ultimo_byte = 0;
    _bytes_no_payload = 0;

    Serial.println("[BiaCamera] Camera ACORDADA e enviando video para o Python!");
    return true;
  }

  int _lerPacote64EP81(uint8_t dev_addr, uint8_t *destino) {
    if (!_cameraPlugada()) return -1;

    _bia_qtd_video[0] = (1UL << 31) | (1UL << 26) | (1UL << 25) | 64;
    _bia_qtd_video[1] = (uint32_t)destino;

    BIA_USB_HCINT1_REG  = 0xFFFFFFFF;
    BIA_USB_HCDMA1_REG  = (uint32_t)&_bia_qtd_video[0];
    BIA_USB_HCTSIZ1_REG = ((uint32_t)_pid_ep81 << 29) | 0xFF;
    BIA_USB_HCCHAR1_REG = (1UL << 31) | ((uint32_t)dev_addr << 22) | (2UL << 18) | (1UL << 15) | (1UL << 11) | 64;

    unsigned long t0 = millis();
    while (BIA_USB_HCCHAR1_REG & (1UL << 31)) {
      if (!_cameraPlugada() || !_deveCapturar() || (BIA_USB_HCINT1_REG & 0x18C) || (millis() - t0 > 50)) {
        BIA_USB_HCCHAR1_REG |= (1UL << 30);
        unsigned long t_halt = millis();
        while ((BIA_USB_HCCHAR1_REG & (1UL << 31)) && (millis() - t_halt < 5)) {}
        return 0;
      }
    }

    if (BIA_USB_HCINT1_REG & 0x01) {
      _pid_ep81 = (BIA_USB_HCTSIZ1_REG >> 29) & 0x03;
      int bytes_lidos = 64 - (_bia_qtd_video[0] & 0x1FFFF);
      return (bytes_lidos > 0) ? bytes_lidos : 0;
    }
    return 0;
  }

  void _restaurarEnumerador() {
    Serial.println("[BiaCamera] Camera desconectada! Aguardando reconexao (Hot-Plug)...");
    if (BIA_USB_HCCHAR0_REG & (1UL << 31)) { BIA_USB_HCCHAR0_REG |= (1UL << 30); vTaskDelay(pdMS_TO_TICKS(2)); }
    if (BIA_USB_HCCHAR1_REG & (1UL << 31)) { BIA_USB_HCCHAR1_REG |= (1UL << 30); vTaskDelay(pdMS_TO_TICKS(2)); }

    BIA_USB_HCCHAR0_REG = 0;
    BIA_USB_HCCHAR1_REG = 0;
    BIA_USB_HCINT0_REG  = 0xFFFFFFFF;
    BIA_USB_HCINT1_REG  = 0xFFFFFFFF;

    _dentro_do_jpeg = false;
    _pos_captura = 0;
    _stream_ativo = false;
    _barramento_suspenso = false;

    BIA_USB_HAINTMSK_REG = _haintmsk_salvo;
    BIA_USB_GAHBCFG_REG  = _gahbcfg_salvo;

    if (_usb_task_hdl != NULL) vTaskResume(_usb_task_hdl);
  }

  static void _taskUsbEvents(void *arg) {
    while (true) {
      uint32_t event_flags;
      usb_host_lib_handle_events(50, &event_flags);
    }
  }

  static void _taskCaptura(void *arg) {
    BiaCameraUVC *self = (BiaCameraUVC *)arg;
    uint16_t pacotes_seguidos = 0;
    bool ja_enumerou_uma_vez = false;

    while (true) {
      // Assim que a camera eh plugada pela primeira vez, captura o controle do enumerador
      if (!ja_enumerou_uma_vez && self->_cameraPlugada() && (BIA_USB_HCCHAR0_REG & 0x00400000)) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!self->_cameraPlugada()) continue;

        if (self->_usb_task_hdl != NULL) vTaskSuspend(self->_usb_task_hdl);
        self->_haintmsk_salvo = BIA_USB_HAINTMSK_REG;
        self->_gahbcfg_salvo  = BIA_USB_GAHBCFG_REG;

        BIA_USB_HAINTMSK_REG = 0;
        BIA_USB_GAHBCFG_REG &= ~1UL;
        ja_enumerou_uma_vez = true;

        // Se o Python ainda nao abriu, ja coloca a camera em Standby imediatamente!
        if (!self->_deveCapturar()) {
          self->_suspenderFisicamenteUSB();
        }
      }

      // 1. Se o Python esta fechado (ou mandou desligar), mantem a camera em Standby!
      if (!self->_deveCapturar()) {
        if (self->_stream_ativo) {
          self->_suspenderFisicamenteUSB();
        }
        if (ja_enumerou_uma_vez && !self->_cameraPlugada()) {
          ja_enumerou_uma_vez = false;
          self->_restaurarEnumerador();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }

      // 2. O Python ABRIU e pediu video! Acorda a camera e inicia o streaming
      if (!self->_stream_ativo) {
        if (!ja_enumerou_uma_vez) {
          vTaskDelay(pdMS_TO_TICKS(50));
          continue;
        }
        if (!self->_cameraPlugada()) {
          ja_enumerou_uma_vez = false;
          self->_restaurarEnumerador();
          continue;
        }

        if (self->_barramento_suspenso) {
          self->_acordarFisicamenteUSB();
        }

        if (self->_iniciarUVC(1)) {
          self->_stream_ativo = true;
        } else {
          ja_enumerou_uma_vez = false;
          self->_restaurarEnumerador();
          vTaskDelay(pdMS_TO_TICKS(500));
        }
        continue;
      }

      // 3. Streaming ativo: verifica desconexao fisica
      if (!self->_cameraPlugada()) {
        ja_enumerou_uma_vez = false;
        self->_restaurarEnumerador();
        continue;
      }

      int lidos = self->_lerPacote64EP81(1, _bia_buf_video);
      if (lidos < 0) {
        ja_enumerou_uma_vez = false;
        self->_restaurarEnumerador();
        continue;
      }

      if (lidos > 0) {
        int inicio_dados = 0;
        if (!self->_dentro_do_jpeg) {
          for (int k = 0; k <= lidos - 3; k++) {
            if (_bia_buf_video[k] == 0xFF && _bia_buf_video[k + 1] == 0xD8 && _bia_buf_video[k + 2] == 0xFF) {
              self->_uvc_hdr_len = k;
              if (k >= 2) self->_uvc_bfh_base = _bia_buf_video[1];
              self->_dentro_do_jpeg = true;
              self->_pos_captura = 0;
              self->_bytes_no_payload = lidos;
              inicio_dados = k;
              break;
            }
          }
        } else {
          self->_bytes_no_payload += lidos;
          if (self->_uvc_hdr_len > 0 && self->_uvc_max_payload > 0 && self->_bytes_no_payload > self->_uvc_max_payload) {
            if (_bia_buf_video[0] == self->_uvc_hdr_len && ((_bia_buf_video[1] & 0xFD) == (self->_uvc_bfh_base & 0xFD))) {
              inicio_dados = self->_uvc_hdr_len;
              self->_bytes_no_payload = lidos;
            }
          }
        }

        if (self->_dentro_do_jpeg) {
          for (int i = inicio_dados; i < lidos; i++) {
            uint8_t b = _bia_buf_video[i];
            if (self->_pos_captura < BIA_MAX_JPEG_SIZE) {
              self->_jpeg_captura[self->_pos_captura++] = b;
            } else {
              self->_dentro_do_jpeg = false;
              break;
            }

            if (self->_ultimo_byte == 0xFF && b == 0xD9) {
              self->_dentro_do_jpeg = false;
              if (self->_pos_captura > 10000) {
                portENTER_CRITICAL(&self->_mux_frame);
                memcpy(self->_jpeg_pronto, self->_jpeg_captura, self->_pos_captura);
                self->_tam_jpeg_pronto = self->_pos_captura;
                self->_id_quadro_pronto++;
                portEXIT_CRITICAL(&self->_mux_frame);
              }
              vTaskDelay(1);
              break;
            }
            self->_ultimo_byte = b;
          }
        }

        if (++pacotes_seguidos >= 120) {
          pacotes_seguidos = 0;
          vTaskDelay(1);
        }
      } else {
        vTaskDelay(1);
      }
    }
  }

  static void _taskEnvioTcp(void *arg) {
    BiaCameraUVC *self = (BiaCameraUVC *)arg;
    uint8_t *buf_envio = (uint8_t *)(psramFound() ? ps_malloc(BIA_MAX_JPEG_SIZE) : malloc(BIA_MAX_JPEG_SIZE));
    WiFiClient client;
    uint32_t ultimo_id = 0;

    while (true) {
      if (WiFi.status() != WL_CONNECTED) {
        if (client.connected()) client.stop();
        self->_python_conectado = false;
        self->_camera_habilitada = false;
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }

      // Tenta conectar no Python. Se o Python estiver fechado, mantem a camera em Standby!
      if (!client.connected()) {
        if (self->_python_conectado) {
          self->_python_conectado = false;
          self->_camera_habilitada = false;
          Serial.println("[BiaCamera] Python desconectou -> Desligando camera (Standby)...");
        }

        if (client.connect(self->_ip, self->_porta, 1000)) {
          client.setNoDelay(true);
          client.setTimeout(2);
          self->_python_conectado = true;
          self->_camera_habilitada = true; // Liga automaticamente ao conectar no Python!
          Serial.printf("[BiaCamera] Python detectado em %s:%d -> Ligando camera!\n", self->_ip, self->_porta);
        } else {
          // Python esta fechado: dorme 1.5s sem gastar bateria
          vTaskDelay(pdMS_TO_TICKS(1500));
          continue;
        }
      }

      // Verifica se o Python enviou comando de controle ('1' = ligar camera, '0' = desligar camera)
      while (client.available() > 0) {
        char cmd = client.read();
        if (cmd == '1') {
          self->setEstado(true);
        } else if (cmd == '0') {
          self->setEstado(false);
        }
      }

      // Se a camera estiver em Standby por comando do Python, apenas mantem o socket vivo esperando '1'
      if (!self->_camera_habilitada) {
        // Envia um heartbeat de 4 bytes zero a cada 500ms para saber se o Python foi fechado
        static unsigned long t_hb = 0;
        if (millis() - t_hb > 500) {
          t_hb = millis();
          uint8_t zero_hdr[4] = {0, 0, 0, 0};
          if (client.write(zero_hdr, 4) != 4) {
            client.stop();
          }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      // Envia os quadros JPEG normalmente enquanto ativa
      if (self->_id_quadro_pronto != ultimo_id && self->_tam_jpeg_pronto > 0) {
        uint32_t tam = 0;
        portENTER_CRITICAL(&self->_mux_frame);
        tam = self->_tam_jpeg_pronto;
        memcpy(buf_envio, self->_jpeg_pronto, tam);
        ultimo_id = self->_id_quadro_pronto;
        portEXIT_CRITICAL(&self->_mux_frame);

        uint8_t hdr[4] = {
          (uint8_t)(tam & 0xFF),
          (uint8_t)((tam >> 8) & 0xFF),
          (uint8_t)((tam >> 16) & 0xFF),
          (uint8_t)((tam >> 24) & 0xFF)
        };

        if (client.write(hdr, 4) != 4 || client.write(buf_envio, tam) != tam) {
          client.stop();
        }
      } else {
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    }
  }
};

static BiaCameraUVC camBia;

#endif