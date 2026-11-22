BiaCameraUVC 📷🤖Uma biblioteca robusta para o ESP32-S3 que permite capturar vídeo de câmeras USB (UVC / MJPEG) via DMA e transmitir em tempo real via Wi-Fi/TCP para o computador (Python).Criada especialmente para projetos de robótica autônoma e visão computacional, ela roda totalmente em segundo plano (FreeRTOS) e foi projetada para ser imune a quedas de rede e falhas físicas.✨ Principais Recursos🔌 Hot-Plug Real: Você pode desplugar e plugar a câmera com o ESP32 ligado, e o vídeo volta automaticamente sem travar o microcontrolador.🔋 Controle Inteligente de Bateria (Standby): A câmera só liga fisicamente quando o seu script Python está aberto solicitando a imagem. Se o Python fechar ou pausar, o ESP32 corta a comunicação USB, colocando a câmera em modo de suspensão de energia.🚀 Zero Travamentos: O loop de captura roda isolado em segundo plano. Você pode controlar motores, matrizes de LED e áudio I2S perfeitamente em paralelo no seu void loop().🐍 Receptor Python Pronto: Acompanha a classe completa BiaCameraReceiver, auto-reconectável e não-bloqueante, pronta para integrar com OpenCV, YOLO e sistemas de visão computacional.⚠️ Requisitos de Hardware e Jumper de 5VPara que o projeto funcione corretamente, preste atenção aos detalhes do hardware:Placa Compatível: ESP32-S3 com PSRAM (recomendado: ESP32-S3 N16R8), pois possui suporte nativo a USB Host Full-Speed via hardware e memória PSRAM para os buffers de vídeo.🔌 O Jumper de 5V (CRÍTICO): A porta USB do ESP32-S3 precisa fornecer 5V (VBUS) para alimentar a câmera externa. Na grande maioria das placas de desenvolvimento (Freenove, YD-ESP32-S3, etc.), essa energia vem desconectada de fábrica. Você precisa localizar o jumper rotulado como USB, OTG ou 5V -> VBUS na placa e fechá-lo (soldar os dois pads). Sem isso, a câmera não ligará!Câmera: Câmeras USB (UVC) genéricas com suporte ao formato MJPEG.🛠️ Como Instalar (Arduino IDE)A biblioteca está disponível oficialmente no Gerenciador de Bibliotecas da Arduino IDE:Abra a Arduino IDE.Clique no ícone do Gerenciador de Bibliotecas (Library Manager) na barra lateral esquerda.Digite BiaCameraUVC na barra de busca.Localize BiaCameraUVC by Rogério Gonçalves e clique em Instalar (Install).Pronto! Agora basta adicionar #include <BiaCameraUVC.h> no topo do seu código.⚙️ Configuração Obrigatória na Arduino IDE (ESP32-S3 N16R8)⚠️ ATENÇÃO — LEIA ANTES DE GRAVAR:Antes de compilar e enviar o código para o seu ESP32-S3 N16R8, confira todos os itens abaixo no menu Ferramentas (Tools) da Arduino IDE. As opções marcadas como CRÍTICO impedem a câmera de inicializar se estiverem configuradas de forma diferente:Configuração (Menu Ferramentas)Valor ObrigatórioPor que isso importa?Placa (Board)ESP32S3 Dev ModulePlaca base padrão para o ESP32-S3.USB ModeHardware CDC and JTAGCRÍTICO: Se ficar em USB-OTG (TinyUSB), o TinyUSB sequestra o controlador OTG e o usb_host_install() da câmera falha.USB CDC On BootDisabledMantém o Monitor Serial funcionando pela porta UART sem brigar com a porta USB da câmera.PSRAMOPI PSRAMCRÍTICO: A câmera aloca 3 buffers de 64 KB (BIA_MAX_JPEG_SIZE) usando ps_malloc quando psramFound() é verdadeiro; sem PSRAM, tenta tirar 192 KB da RAM interna e falha.Flash Size16MB (128Mb)Tamanho total da memória flash do módulo N16R8.Flash ModeQIO 80MHzModo e velocidade de comunicação da flash.Partition Scheme16M Flash (3MB APP/9.9MB FATFS)Garante folga de sobra para firmwares maiores e uso de ArduinoOTA.Upload Speed921600Gravação rápida (se falhar o upload, abaixe para 460800).💻 Exemplo Prático: Código do ESP32-S3Veja como é simples integrar a câmera no seu projeto sem travar o void loop(). Substitua SEU_WIFI, SUA_SENHA e o SERVIDOR_IP (IP local do computador rodando o script Python):#include <Arduino.h>
#include <WiFi.h>
#include <BiaCameraUVC.h>

const char *ssid = "SEU_WIFI";
const char *password = "SUA_SENHA";
const char *SERVIDOR_IP = "192.168.18.22"; // IP do PC rodando o Python
const int PORTA_VIDEO_PYTHON = 5005;

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // OBRIGATÓRIO: impede hibernação do Wi-Fi e quedas de FPS
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[+] Wi-Fi conectado! IP: %s\n", WiFi.localIP().toString().c_str());

  // Inicia em Standby automático: só liga a câmera quando o Python abrir!
  camBia.begin(SERVIDOR_IP, PORTA_VIDEO_PYTHON);
}

void loop() {
  // Nada aqui! O próprio Python liga e desliga a câmera no ESP32 automaticamente.
  // Seu loop fica 100% livre para motores, sensores e outras tarefas.
  delay(1000);
}
🐍 Código do Receptor no PC (Python)1. Instale as dependênciasNo terminal do seu computador, instale o OpenCV e o NumPy:pip install opencv-python numpy
2. Salve e execute o receptor (bia_camera.py)Salve o código abaixo como bia_camera.py e execute. Ele já abre a janela ao vivo automaticamente e pode ser importado em outros projetos de IA/Visão Computacional:import socket
import struct
import threading
import time
import cv2
import numpy as np


class BiaCameraReceiver:
    """Biblioteca receptora de video TCP da Bia com controle remoto de energia (Standby)."""

    def __init__(self, porta=5005, host="0.0.0.0", timeout_seg=2.5, iniciar_ativa=True):
        self.porta = porta
        self.host = host
        self.timeout_seg = timeout_seg

        self._rodando = False
        self._conectado = False
        self._camera_ativa_remota = iniciar_ativa
        self._sem_sinal = True
        self._fps = 0.0
        self._frame_atual = None
        self._conn_atual = None
        self._lock = threading.Lock()
        self._thread = None

    def iniciar(self):
        """Abre a porta no PC e acorda a camera no ESP32 automaticamente."""
        if self._rodando:
            return
        self._rodando = True
        self._thread = threading.Thread(target=self._loop_servidor, daemon=True)
        self._thread.start()
        print(
            f"[BiaCamera] Servidor Python ativo na porta {self.porta} (Camera solicitada: {'SIM' if self._camera_ativa_remota else 'STANDBY'})"
        )

    def parar(self):
        """Manda o ESP32 desligar a camera (Standby) e fecha o servidor do Python."""
        self.desligar_camera()
        time.sleep(0.1)
        self._rodando = False
        if self._thread:
            self._thread.join(timeout=2.0)
        print("[BiaCamera] Receptor encerrado. Camera do ESP32 em Standby.")

    def ligar_camera(self):
        """Envia comando para o ESP32 acordar a camera do modo Standby."""
        self._camera_ativa_remota = True
        self._enviar_comando_esp(b"1")
        print("[BiaCamera] Comando enviado ao ESP32: LIGAR CAMERA")

    def desligar_camera(self):
        """Envia comando para o ESP32 colocar a camera em Standby (Economia de bateria)."""
        self._camera_ativa_remota = False
        self._enviar_comando_esp(b"0")
        with self._lock:
            self._sem_sinal = True
            self._fps = 0.0
        print("[BiaCamera] Comando enviado ao ESP32: DESLIGAR CAMERA (Standby)")

    def _enviar_comando_esp(self, byte_cmd):
        with self._lock:
            if self._conn_atual is not None:
                try:
                    self._conn_atual.sendall(byte_cmd)
                except Exception:
                    pass

    def ler_frame(self):
        """Retorna (sucesso, frame_bgr, fps). Nunca bloqueia o programa principal."""
        with self._lock:
            if self._frame_atual is None:
                return False, None, 0.0
            return (
                (not self._sem_sinal and self._camera_ativa_remota),
                self._frame_atual.copy(),
                self._fps,
            )

    @staticmethod
    def _receber_exatos(sock, tamanho):
        dados = bytearray()
        while len(dados) < tamanho:
            pacote = sock.recv(tamanho - len(dados))
            if not pacote:
                return None
            dados.extend(pacote)
        return dados

    def _loop_servidor(self):
        servidor = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        servidor.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        servidor.bind((self.host, self.porta))
        servidor.listen(1)
        servidor.settimeout(0.5)

        try:
            while self._rodando:
                try:
                    conn, addr = servidor.accept()
                except socket.timeout:
                    continue

                print(f"[BiaCamera] ESP32-S3 conectado ({addr[0]}:{addr[1]})")
                conn.settimeout(self.timeout_seg)
                with self._lock:
                    self._conn_atual = conn
                    self._conectado = True

                # Envia imediatamente o estado desejado ('1' para ligar ou '0' para Standby)
                self._enviar_comando_esp(
                    b"1" if self._camera_ativa_remota else b"0"
                )

                quadros = 0
                t_inicio = time.time()

                try:
                    while self._rodando:
                        try:
                            cabecalho = self._receber_exatos(conn, 4)
                            if not cabecalho:
                                break

                            (tam_jpeg,) = struct.unpack("<I", cabecalho)

                            # Se tam_jpeg == 0, eh apenas o Heartbeat do ESP32 avisando que esta vivo em Standby!
                            if tam_jpeg == 0:
                                continue

                            if tam_jpeg > 100000:
                                break

                            bytes_jpeg = self._receber_exatos(conn, tam_jpeg)
                            if not bytes_jpeg:
                                break

                            arr = np.frombuffer(bytes_jpeg, dtype=np.uint8)
                            frame = cv2.imdecode(arr, cv2.IMREAD_COLOR)

                            if frame is not None:
                                quadros += 1
                                agora = time.time()
                                if agora - t_inicio >= 1.0:
                                    self._fps = quadros / (agora - t_inicio)
                                    quadros = 0
                                    t_inicio = agora

                                with self._lock:
                                    self._frame_atual = frame
                                    self._sem_sinal = False

                        except socket.timeout:
                            with self._lock:
                                self._sem_sinal = True
                                self._fps = 0.0

                            try:
                                nova_conn, novo_addr = servidor.accept()
                                print(
                                    f"[BiaCamera] Reconexao imediata de {novo_addr[0]}:{novo_addr[1]}"
                                )
                                conn.close()
                                conn = nova_conn
                                conn.settimeout(self.timeout_seg)
                                with self._lock:
                                    self._conn_atual = conn
                                self._enviar_comando_esp(
                                    b"1" if self._camera_ativa_remota else b"0"
                                )
                                quadros = 0
                                t_inicio = time.time()
                                continue
                            except socket.timeout:
                                pass

                except (ConnectionResetError, ConnectionAbortedError, OSError):
                    pass
                finally:
                    with self._lock:
                        self._conectado = False
                        self._sem_sinal = True
                        self._conn_atual = None
                    try:
                        conn.close()
                    except Exception:
                        pass
                    if self._rodando:
                        print("[BiaCamera] Aguardando reconexao do ESP32-S3...")
        finally:
            servidor.close()

    def exibir_janela_ao_vivo(self, titulo="Camera ESP32-S3 - Bia"):
        """Abre a janela OpenCV. Pressione 's' para alternar Standby ou 'q' para sair."""
        self.iniciar()
        tela_espera = np.zeros((480, 640, 3), dtype=np.uint8)
        cv2.putText(
            tela_espera,
            f"Acordando camera do ESP32 na porta {self.porta}...",
            (35, 230),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.65,
            (0, 165, 255),
            2,
        )
        cv2.putText(
            tela_espera,
            "Tecla 's' = Standby (Pausar) | Tecla 'q' = Sair (Desliga Camera)",
            (35, 270),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (180, 180, 180),
            1,
        )

        try:
            while True:
                ativo, frame, fps = self.ler_frame()
                if frame is not None:
                    vis = frame.copy()
                    if ativo:
                        cv2.putText(
                            vis,
                            f"Bia Cam | {fps:.1f} FPS | ['s'=Standby | 'q'=Sair]",
                            (10, 28),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.62,
                            (0, 255, 0),
                            2,
                        )
                    else:
                        cv2.rectangle(vis, (0, 435), (640, 480), (0, 0, 0), -1)
                        msg = (
                            "[MODO STANDBY] Camera desligada poupando bateria (Aperte 's' p/ ligar)"
                            if not self._camera_ativa_remota
                            else "[!] Sem sinal da camera..."
                        )
                        cv2.putText(
                            vis,
                            msg,
                            (12, 463),
                            cv2.FONT_HERSHEY_SIMPLEX,
                            0.50,
                            (0, 165, 255),
                            2,
                        )
                    cv2.imshow(titulo, vis)
                else:
                    cv2.imshow(titulo, tela_espera)

                tecla = cv2.waitKey(15) & 0xFF
                if tecla == ord("q") or tecla == 27:
                    break
                elif tecla == ord("s"):
                    # Alterna entre ligada e Standby apertando 's' no teclado!
                    if self._camera_ativa_remota:
                        self.desligar_camera()
                    else:
                        self.ligar_camera()
        finally:
            self.parar()
            cv2.destroyAllWindows()


if __name__ == "__main__":
    cam = BiaCameraReceiver(porta=5005)
    cam.exibir_janela_ao_vivo()
3. Uso rápido importando em outro projeto PythonSe quiser usar o bia_camera.py dentro de um projeto maior (como OpenCV ou YOLO), bastam 3 linhas:from bia_camera import BiaCameraReceiver

cam = BiaCameraReceiver(porta=5005)
cam.exibir_janela_ao_vivo()
💡 Dica de Ouro (Controle Remoto de Bateria):Com a janela de vídeo aberta, pressione a tecla 's' para pausar o vídeo e colocar a câmera no ESP32 em Standby (economizando bateria). Pressione 's' novamente para acordá-la. Ao fechar o script (tecla 'q' ou ESC), a câmera desliga automaticamente.
