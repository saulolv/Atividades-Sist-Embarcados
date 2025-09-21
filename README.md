# Atividades de Sistemas Embarcados

Este repositório contém exemplos e exercícios práticos para o curso de sistemas embarcados, utilizando Zephyr RTOS. As atividades abordam conceitos fundamentais como GPIO, PWM, timers e uso do sistema de logs.

---

## Atividade 1 – Hello World com Timer

### Objetivos

- Implementar um Hello World periódico utilizando a API de timer do Zephyr.
- Utilizar diferentes níveis de log para exibir a mensagem.
- Configurar, via Kconfig, o intervalo de repetição da mensagem.

### Etapas

1. **Configuração inicial**
   - Crie um projeto Zephyr básico.
   - Habilite o módulo de log no arquivo `prj.conf`.
   - Defina uma opção no `Kconfig` para configurar o intervalo do timer.

2. **Implementação do timer**
   - Implemente um timer periódico usando a API de timers do Zephyr.
   - No callback do timer, imprima a mensagem “Hello World”.
   - O intervalo do timer deve ser configurável via Kconfig.

3. **Uso dos níveis de log**
   - Utilize diferentes níveis de log (`LOG_INF`, `LOG_DBG`, `LOG_ERR`) para exibir a mensagem.
   - Teste a alteração do nível de log no `prj.conf` e observe o comportamento.

---

## Atividade 2 – Controle de Brilho de LED com GPIO, PWM e Botão

### Objetivos

- Compreender o uso de GPIO como entrada e saída.
- Aplicar PWM para controlar o brilho de um LED.
- Implementar interação entre botão e LED.

### Etapas

1. **Configuração simples**
   - Configure um pino GPIO como saída.
   - Escreva um código para ligar e desligar o LED.
   - Ajuste o tempo de piscar do LED.

2. **Controle do LED com botão**
   - Configure outro pino GPIO como entrada para o botão.
   - Altere o comportamento do LED quando o botão for pressionado.

3. **Controle do brilho via PWM**
   - Configure um pino com função PWM.
   - Implemente a variação do duty cycle para modificar o brilho do LED.
   - Crie um efeito de transição de brilho (fade in/fade out).

4. **Integração botão + PWM**
   - Defina dois modos de operação:
     - **Modo 1:** LED acende/apaga normalmente (digital).
     - **Modo 2:** LED apresenta variação gradual de brilho (PWM).
   - Use o botão para alternar entre os modos.

---

## Observações

- Utilize o Zephyr RTOS e consulte a documentação oficial para detalhes sobre APIs de GPIO, PWM, timers e logs.
- Os parâmetros de configuração devem ser definidos nos arquivos `prj.conf` e `Kconfig` do projeto.
- Teste as funcionalidades em hardware compatível ou emuladores suportados pelo Zephyr.

---