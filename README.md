# BiaCameraUVC 📷🤖

Uma biblioteca robusta para o **ESP32-S3** que permite capturar vídeo de câmeras USB (UVC / MJPEG) via DMA e transmitir em tempo real via Wi-Fi/TCP para o computador (Python). 

Criada especialmente para projetos de robótica autônoma e visão computacional, ela roda totalmente em segundo plano (FreeRTOS) e foi projetada para ser imune a quedas de rede e falhas físicas.

## ✨ Principais Recursos

* **🔌 Hot-Plug Real:** Você pode desplugar e plugar a câmera com o ESP32 ligado, e o vídeo volta automaticamente sem travar o microcontrolador.
* **🔋 Controle Inteligente de Bateria (Standby):** A câmera só liga fisicamente quando o seu script Python está aberto solicitando a imagem. Se o Python fechar, o ESP32 corta a comunicação USB, colocando a câmera em modo de suspensão de energia.
* **🚀 Zero Travamentos:** O loop de captura roda isolado. Você pode controlar motores, matrizes de LED e áudio I2S perfeitamente em paralelo no seu `void loop()`.
* **🐍 Módulo Python Incluso:** Acompanha uma classe Python (`BiaCameraReceiver`) auto-reconectável, pronta para integrar com OpenCV, YOLO e IA de visão computacional.

---

## ⚠️ Requisitos de Hardware e Jumper de 5V

Para que o projeto funcione corretamente, preste atenção aos detalhes do hardware:

1. **Placa Compatível:** ESP32-S3 (Obrigatório, pois possui suporte nativo a USB Host em Full-Speed via hardware).
2. **🔌 O Jumper de 5V (CRÍTICO):** A porta USB do ESP32-S3 precisa fornecer 5V (VBUS) para ligar a câmera externa. Na grande maioria das placas de desenvolvimento (como as da Freenove, YD-ESP32-S3, etc.), a energia não vai para a porta USB por padrão. **Você precisa localizar um pequeno jumper na placa rotulado como "USB", "OTG" ou "5V a VBUS" e fechá-lo (soldar os dois pads)**. Se você não fizer isso, a câmera não ligará!
3. **Câmera:** Câmeras UVC genéricas com suporte ao formato MJPEG.

---

## 🛠️ Como Instalar (Arduino IDE)

Para usar em qualquer projeto seu:
1. Baixe o arquivo `BiaCameraUVC.h` deste repositório.
2. No seu computador, vá até a pasta `Documentos/Arduino/libraries/`.
3. Crie uma nova pasta chamada `BiaCameraUVC` e cole o arquivo lá dentro.
4. Reinicie a IDE Arduino e pronto! Agora você pode usar `#include <BiaCameraUVC.h>` em qualquer código.

---

## 💻 Exemplo Prático: Código do ESP32-S3

Veja como é simples integrar a câmera no seu robô sem travar o seu `void loop()`. O código principal fica totalmente livre para outras funções:

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include "BiaCameraUVC.h" // Nossa biblioteca mágica!

const char *ssid = "SEU_WIFI";
const char *password = "SUA_SENHA";
const char *SERVIDOR_IP = "192.168.18.22"; // IP do seu PC rodando o Python
const int PORTA_VIDEO = 5005;

void setup() {
  Serial.begin(115200);
  
  // 1. Conexão Wi-Fi padrão
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // OBRIGATÓRIO: Impede que o Wi-Fi hiberne e cause lentidão no vídeo
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  // 2. Inicia a câmera em segundo plano!
  // Ela já inicia em modo Standby (consumo mínimo de bateria) aguardando o Python abrir.
  camBia.begin(SERVIDOR_IP, PORTA_VIDEO);
}

void loop() {
  // O seu loop fica 100% LIVRE!
  // Controle seus motores, leia sensores ou processe áudio aqui.
  // Toda a negociação USB, reconexão TCP e streaming rola nos bastidores.
  
  delay(10);
}
```

---

## 🐍 Exemplo Prático: Código do Python (PC)

No lado do computador, o receptor resume toda a complexidade de TCP e quedas de rede a apenas **3 linhas de código**:

```python
from funcoes.bia_camera import BiaCameraReceiver

# 1. Cria o receptor apontando para a mesma porta do ESP32
cam = BiaCameraReceiver(porta=5005)

# 2. Abre a janela de vídeo ao vivo
# (O ESP32 acorda a câmera automaticamente assim que essa função roda!)
cam.exibir_janela_ao_vivo()
```

> **💡 Dica de Ouro (Controle Remoto de Bateria):** 
> Com a janela de vídeo aberta, pressione a **tecla 's'** para pausar o vídeo e forçar a câmera no ESP32 a entrar em Standby (economizando energia). Pressione 's' novamente para acordá-la. Ao fechar o script (tecla 'q'), a câmera sempre desliga sozinha para poupar bateria do seu robô.