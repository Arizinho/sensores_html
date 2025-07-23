# Estação Meteorológica com Raspberry Pi Pico W

Este projeto usa o Raspberry Pi Pico W para monitorar temperatura, umidade, pressão e altitude.  
Ele exibe as informações em um display OLED, emite alertas com buzzer, LEDs RGB e matriz de LEDs, e disponibiliza uma interface web.

## Funcionalidades

- Leitura dos sensores **AHT20** e **BMP280** para coletar temperatura, umidade e pressão  
- Cálculo da **altitude** com base na pressão medida  
- **Display OLED SSD1306** mostra os valores atuais ou o IP do dispositivo, alternando pelo botão A  
- **Interface Web** exibe valores em tempo real, gráficos e permite configurar limites de temperatura e umidade  
- **Matriz de LEDs 5x5** acende em padrão de alerta  
- **LED RGB** muda de cor para indicar estado (verde normal, vermelho alerta)  
- **Buzzer PWM** emite alerta sonoro quando os limites são ultrapassados  
