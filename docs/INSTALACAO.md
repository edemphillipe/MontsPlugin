# Instalação do Guitar Studio Pro (versão de teste)

## Onde baixar

1. Abra o repositório no GitHub e clique na aba **Actions**.
2. Clique na execução mais recente com o ícone verde de sucesso.
3. Em **Artifacts**, baixe:
   - **GuitarStudioPro-macOS**: dentro do .zip está o instalador `GuitarStudioPro-<versão>-macOS.dmg`;
   - **GuitarStudioPro-Windows**: dentro do .zip está o instalador `GuitarStudioPro-<versão>-Windows-Setup.exe`
     (e uma versão portátil, `...-Windows-portatil.zip`, que roda sem instalar).

## macOS

1. Abra o .dmg e arraste **Guitar Studio Pro** para **Aplicativos**.
2. Esta versão ainda não é assinada pela Apple. Na primeira vez:
   clique com o botão direito no app → **Abrir** → **Abrir**.
   Se o macOS bloquear, vá em **Ajustes do Sistema → Privacidade e Segurança** e clique em **Abrir Mesmo Assim**.
   Isso só é preciso uma vez (e de novo a cada versão nova).
3. Permita o acesso ao **Microfone** quando o macOS pedir (é assim que o app recebe o sinal da interface de áudio).

Funciona em Macs com Apple Silicon (M1 a M4 e posteriores) e Intel, macOS 11 ou mais novo.
Para atualizar, arraste a versão nova para Aplicativos e escolha **Substituir**: pedaleiras, capturas e gravações ficam onde estão.

## Windows

1. Abra o **...-Windows-Setup.exe**. Se aparecer o aviso do SmartScreen, clique em **Mais informações → Executar assim mesmo**
   (esta versão ainda não tem certificado de assinatura).
2. Siga o instalador. Ele coloca o app em *Arquivos de Programas*, cria o atalho no menu Iniciar
   (e na área de trabalho, se você marcar) e aparece em **Configurações → Aplicativos** para desinstalar.
3. Para atualizar, rode o instalador da versão nova por cima. Pedaleiras, capturas e gravações não são apagadas.
4. Para a menor latência, use o driver **ASIO** da sua interface de áudio.

## Primeiro uso

1. Clique em **Configurar áudio**, escolha sua interface, a taxa (48 kHz recomendado) e o buffer (64 ou 128).
2. No seletor ao lado, escolha a **entrada** onde a guitarra está ligada.
3. Clique em **Pasta...** na biblioteca e escolha a pasta onde estão suas capturas `.nam` e AIDA-X `.json`.
   Organize em subpastas, por exemplo `Capturas/Marshall/JCM800/`, e o app mostra "Marshall / JCM800".
4. Dê duplo clique em uma captura para carregá-la. Clique na estrela para favoritar.
5. Em **Gabinete IR**, carregue um IR (.wav) se a captura for só do amplificador (sem gabinete).

Dica: capturas de amplificador "full rig" já incluem o gabinete; nesse caso desligue o IR (LED verde do painel).
