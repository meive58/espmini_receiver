# espmini_receiver
gerado com Claude Sonnet 5.5

Como funciona

- O ESP32-C3 cria o Wi-Fi RoboCombate (senha 12345678, ambos configuráveis no topo do arquivo).
- No celular, conecte nessa rede e abra http://192.168.4.1. A página tem um joystick para locomoção, um botão de armar e um controle deslizante para a velocidade da arma. Não precisa instalar nada.
- O celular envia os comandos a cada 50 ms. O ESP gera os sinais PWM de 50 Hz, com pulsos de 1000 a 2000 µs e neutro em 1500 µs.

Ligações

Função	                             | Pino do ESP
-------------------------------------|-------------------------
ESC locomoção, canal 1 (fio amarelo) |	GPIO0
-------------------------------------|-------------------------
ESC locomoção, canal 2	             | GPIO1
-------------------------------------|-------------------------
ESC arma, canal 1	                 | GPIO3
-------------------------------------|-------------------------
Terra dos ESCs	                     | GND do ESP (obrigatório)

Locomoção

- Por padrão o ESC fica em modo independente e o ESP faz a mistura. Assim o canal 1 comanda o motor esquerdo e o canal 2 o direito.
- Se preferir o modo misto do próprio ESC, coloque MIX_IN_ESP = false.
- Se algum motor girar ao contrário, use INVERT_LEFT ou INVERT_RIGHT.

Segurança

- Se o ESP ficar mais de 300 ms sem receber comando, todas as saídas voltam ao neutro.
- A arma só gira com o botão de armar ativo. Ao desarmar, ela para na hora.
- A arma só gira para frente, com rampa de subida de 1000 µs/s (configurável).
- No boot o ESP já manda neutro, que o ESC precisa para armar.

Placa e Wi-Fi

- Na Arduino IDE, escolha a placa "ESP32C3 Dev Module" e ative "USB CDC On Boot".
- O sketch funciona com o core ESP32 versão 2.x ou 3.x.
- Se o Wi-Fi do SuperMini ficar instável, descomente a linha WiFi.setTxPower(...) no setup().
- O Android pode desconectar por a rede não ter internet. Aceite "manter conectado".