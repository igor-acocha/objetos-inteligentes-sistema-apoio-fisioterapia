# objetos-inteligentes-sistema-apoio-fisioterapia
O trabalho tem como objetivo apresentar a proposta de um sistema para apoio ao tratamento de fisioterapia através da criação de um dispositivo que utiliza recursos de IoT (Internet of Things).
O projeto foi desenvolvido por meio da plataforma www.wokwi.com e está disponível no link https://wokwi.com/projects/443477494248782849

Este documento explica, de forma simples, alguns testes que você pode realizar usando o simulador Wokwi e o dashboard no Adafruit IO.

##Quantas repetições foram detectadas

- Se o movimento foi realizado com boa qualidade
- Se o ritmo (RPM) está adequado
- Se os dados estão chegando no Adafruit IO sem atraso
- Como validar o funcionamento geral do sistema

🧪 1. Teste de Detecção de Repetições
✔ Objetivo: Verificar se o sistema conta corretamente cada repetição do exercício.

📝 Como fazer: No Wokwi, abra o painel do sensor MPU6050.

* Encontre o controle chamado ACCELERATION Z.
* Faça o seguinte movimento manualmente:
** Suba o valor de Z para acima de 0.5
** Depois desça o Z para abaixo de 0.25

-> Isso simula uma repetição.

🧭 O que você deve observar:
* O visor OLED mostra “Meta: X/Y”
* O número de repetições deve aumentar no Adafruit IO
* O valor deve aparecer no feed repeticoes

🎯 Sucesso quando:
* Cada ciclo “sobe > desce” conta UMA repetição.

🧪 2. Teste de Qualidade da Repetição
✔ Objetivo: Verificar se o sistema consegue avaliar como a repetição foi feita.

📝 Como fazer: 
* Movimente o slider ACCELERATION Z de maneira suave e lenta
** isso simula um movimento bem feito
* Depois faça o mesmo movimento, mas variando Z de forma irregular
** isso simula um movimento tremido ou mal executado

🧭 O que você deve observar:
* No OLED:
** Qult: qualidade da última repetição
** Qavg: qualidade média da sessão
* No dashboard:
** gauge ou gráfico de qualidade_rep
** gráfico de qualidade_media_sessao

🎯 Sucesso quando:
* Movimentos suaves geram pontuação alta (>=50%)
* Movimentos irregulares geram pontuação baixa (<=40%)

🧪 3. Teste de Ritmo / RPM (Frequência)
✔ Objetivo: Testar se o sistema calcula corretamente o ritmo das repetições.

📝 Como fazer:
* Faça uma série de repetições rápidas
* Depois faça repetições lentas

🧭 O que o dashboard deve mostrar:
* Gráfico do feed frequencia
* Valores típicos:
** 0–10 RPM: movimento muito lento
** 10–20 RPM: ritmo normal
** 20–34 RPM: ritmo mais rápido
** >35 RPM: ritmo muito alto e não recomendável

🎯 Sucesso quando:
* A linha do gráfico aumenta quando você faz movimentos mais rápidos.

🧪 4. Teste do Score Médio da Sessão
✔ Objetivo: Conferir se a plataforma calcula a média de qualidade após várias repetições.

📝 Como fazer:
* Faça 5 repetições suaves (qualidade alta)
* Depois 5 repetições irregulares (qualidade baixa)

🧭 Resultado esperado:
* Qult (última repetição) varia bastante
* Qavg (média da sessão) deve:
* Começar alto
* Cair aos poucos conforme repetições ruins aparecem

🎯 Sucesso quando:
* A média reflete corretamente a performance geral.
