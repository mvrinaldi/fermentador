Altere o sistema para que o ESP8266 assuma o controle total das trocas de fase, transformando o Firebase em apenas um espelho do que acontece no controlador, em vez de ser o "cérebro".
Com base nas estruturas já presentes nos arquivos (especialmente em estruturas.h e fermentacao_firebase.cpp), aqui está como essa lógica funcionaria e o que precisaria ser alterado:
1. Requisitos para a Mudança
Para que o ESP8266 controle as fases, ele precisa ter a "partitura" completa da fermentação guardada no Firebase. Atualmente, o código busca apenas a etapa atual.
• Busca Completa: A função loadConfigParameters precisaria ser modificada para baixar o objeto /configurations/{id} que está ativo inteiro, em especial stages, preenchendo o array stages dentro da estrutura FermentationConfig.
• Persistência de Tempo: O ESP8266 precisaria registrar o momento exato em que a temperatura de uma etapa foi atingida  (startTime) para calcular a duração em dias ou gravity (no stage há o "type": temperatura, ramps ou gravity). Se a etapa for de 18 graus por 5 dias e a temperatura atual for de 25 graus, inicia o controle da temperatura para trazer para 18 graus mas só começa a contar quando chegar nos 18 graus e aí não para mais ainda que a temperatura saia do target. Se o type for "temperatura", o sistema fica na temperatura pelo tempo definido. Se for do type "gravity", fica na temperatura até atingir a gravidade, vinda do iSpindel. Se for "gravity_time", fica na temperatura até atingir a gravidade ou passar o tempo definido (timeout)
2. A Lógica de Transição no ESP8266
Vou precisar de uma nova função (ex: verificarTrocaDeFase) rodando no loop() que execute estas verificações:
• Fases por Tempo: Se type for "temperature" ou "ramp", o ESP8266 compararia o tempo atual com o durationDays.
    ◦ Cálculo: (millis() - etapaStartTime) > (durationDays * 86400000).
• Fases por Gravidade: Como o sistema já recebe dados do iSpindel localmente via setupSpindelRoutes, o ESP8266 tem acesso imediato à variável mySpindel.gravity.
    ◦ Cálculo: Se o type for "gravity" e mySpindel.gravity <= targetGravity, o ESP8266 decide avançar para a próxima etapa.
• Fases por Gravidade e tempo: Verifica se a gravidade foi atingida antes do tempo. Se não, avança ao atingir o tempo
• Se a fase for de rampa: A rampa é o tempo que leva para sair de uma fase e chegar em outra. Por exemplo, definido que leva 12 horas para sair da fase 1, que estava a 18 graus, até chegar na fase 2 que é 22 graus, calcula o tempo e sobe a temperatura (ou desce se for o caso), gradativamente evitando subidas ou descidas bruscas.
    
3. Informando o Firebase
Assim que o ESP8266 detectar que uma fase terminou:
1. Ele incrementa o currentStageIndex localmente.
2. Ele atualiza o targetTemp para o valor da nova etapa.
3. Ele envia um comando Database.set para o Firebase atualizando o caminho /active/currentStageIndex no id. Isso garante que seu aplicativo ou painel web mostre que a fase mudou.
Benefícios dessa Abordagem:
• Autonomia: Se a internet cair, o ESP8266 continuará trocando as fases no tempo certo, pois ele já tem o plano completo na memória.
• Guarda os dados importantes no EEPROM para evitar que um reinício do esp faça com que os dados sejam perdidos
• Precisão na Gravidade: A troca por gravidade torna-se instantânea, pois o dado do iSpindel chega direto no ESP8266 e ele já toma a decisão, sem esperar o atraso de envio/processamento na nuvem.
O que mudar no código atual:
• fermentacao_firebase.cpp: Alterar loadConfigParameters para desserializar todas as etapas no array stages (podem ser até 10).
• main.cpp: Adicionar uma chamada no loop() para uma função que monitore o progresso da etapa atual (tempo ou gravidade) e atualize o fermentacaoState.tempTarget sempre que o índice da etapa mudar.