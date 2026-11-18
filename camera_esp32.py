import os
import cv2
from funcoes.bia_camera import BiaCameraReceiver

# 1. Instancia o receptor na porta 5005 (iniciar_ativa=True acorda a camera do ESP32 ao conectar)
cam = BiaCameraReceiver(porta=5005, iniciar_ativa=True)

print("[*] Iniciando conexao com o ESP32 para capturar uma foto...")
cam.iniciar()

print("[*] Aguardando estabilizacao da camera e o primeiro frame...")
frame_capturado = None
tempo_inicio = cv2.getTickCount()

# Tenta capturar um frame válido por até 6 segundos
while (cv2.getTickCount() - tempo_inicio) / cv2.getTickFrequency() < 6.0:
    ativo, frame, fps = cam.ler_frame()
    if ativo and frame is not None:
        frame_capturado = frame
        break
    cv2.waitKey(100)

# 2. Verifica se a foto foi obtida com sucesso
if frame_capturado is not None:
    # Salva a foto na pasta atual
    nome_arquivo = "foto_bia.jpg"
    cv2.imwrite(nome_arquivo, frame_capturado)
    print(f"[+] Foto capturada e salva com sucesso como '{nome_arquivo}'!")

    # Exibe a foto na tela por alguns instantes (pressione qualquer tecla para fechar)
    cv2.imshow("Foto Tirada pela Bia", frame_capturado)
    print("[*] Pressione qualquer tecla na janela da foto para fechar...")
    cv2.waitKey(0)
    cv2.destroyAllWindows()
else:
    print(
        "[-] Nao foi possivel capturar a imagem (ESP32 desligado ou fora de alcance)."
    )

# 3. Encerra o receptor: isso fecha a conexao TCP e comanda o ESP32 a desligar a camera na hora!
print("[*] Desligando a camera do ESP32 para economizar bateria...")
cam.parar()
print("[*] Pronto! Câmera em Standby.")
