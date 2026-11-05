<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [한국어](README.ko.md)<br>
[Français](README.fr.md) · [Deutsch](README.de.md) · [Español](README.es.md) · **Português (Brasil)** · [Русский](README.ru.md)

**Tradução em tempo real do chat de World of Warcraft.** Leia mensagens no idioma escolhido e traduza suas respostas em uma sobreposição compacta.

[Compilar o código](docs/BUILDING.md) · [Privacidade](PRIVACY.md)

BianLingo é um projeto de fãs gratuito e não comercial. Não tem vínculo com a Blizzard Entertainment nem sua aprovação ou endosso.

## Recursos

- **Tradução local ou on-line**: Bergamot, CTranslate2, Ollama ou conexão com endereço, modelo e chave de API próprios. Suporta formatos compatíveis com OpenAI, Anthropic e Gemini.
- **Preparação antes de jogar**: escolha o idioma de destino e baixe os pares necessários. Os idiomas disponíveis dependem do mecanismo e do modelo.
- **Sobreposição de chat**: mova e redimensione a janela, ajuste transparência do fundo e fonte, escolha temas ou estilos personalizados.
- **Abas de chat**: nomes e filtros independentes, com importação da configuração exportada pelo addon.
- **Resposta por clique**: clique esquerdo prepara a resposta no canal da mensagem. O menu direito oferece sussurro, consulta manual de nível e bloqueio. Enter traduz e copia; você cola e envia no jogo.
- **Filtros**: canais, ruído e nomes que usam a escrita do idioma de destino. Proteção de links e termos do jogo; prompts separados para mensagens recebidas e respostas.
- **Reenvio manual**: cache em memória de 10 mensagens completas por padrão, ajustável de 1 a 100. Clique direito no minimapa ou `/bl replay` reenvia o cache; duplicatas não são traduzidas de novo.
- **Pixels e OCR**: matriz de cores compacta ou grade binária, ocultação em inatividade com pequeno marcador de estado. No Windows, OCR de uma área de chat selecionada.
- **Logs opcionais**: desativados por padrão; acrescentam mensagens recebidas e traduções de saída confirmadas a arquivos diários UTF-8.
- **Dados portáteis**: configurações, logs, modelos baixados e caches gerenciados pelo aplicativo ficam em `UserData`, ao lado de `BianLingo.exe`.

## Opções de tradução

| Mecanismo | Recursos | Limitações | Uso |
|---|---|---|---|
| **API on-line** | Prompts e estilos, sem baixar modelos | Rede, cotas, tarifas e políticas de dados do provedor | Controle por prompt sem inferência local |
| **Ollama** | LLM local com prompts e estilos | Download de ambiente e modelo; memória e latência conforme o hardware | Processamento local com prompts |
| **Bergamot** | Tradução por CPU off-line, sem tarifa de API | Pares preparados; sem prompts | Par compatível off-line |
| **CTranslate2** | Tradução por CPU INT8 off-line, sem tarifa de API | Pares OPUS-MT preparados; sem prompts | Par OPUS-MT off-line |

O roteamento automático do Bergamot/CTranslate2 pressupõe texto em inglês; para destino inglês, escolhe entrada em chinês pela presença de caracteres Han. Baixar mais pares não permite identificar automaticamente qualquer idioma de origem. O processamento local não envia chat a um provedor de tradução. Latência e resultado dependem de modelo, par, mensagem e condições de hardware/rede.

### Benchmark · AMD Ryzen 7 9700X

| Método | Mediana EN→ZH / ZH→EN | Pico de RAM | CPU contínua | Respostas válidas | Observações |
|---|---:|---:|---:|---:|---|
| **Bergamot** | **13 / 19 ms** | **439 MiB** | **3.3%** | 72/72 | Menor latência; erros em algumas abreviações e termos do jogo |
| CTranslate2 | 64 / 90 ms | 450 MiB | 18.1% | 72/72 | Baixa latência; erros de terminologia do jogo |
| Qwen3 0.6B | 181 / 99 ms | 1.09 GiB | 49.6% | 69/72 | Omissões e cópia de exemplos do prompt |
| Qwen3 1.7B | 479 / 515 ms | 1.89 GiB | 49.6% | 72/72 | Respostas completas, mas erros de tradução significativos |

Medições dos mecanismos em CPU para inglês ↔ chinês simplificado. A CPU indica carga de tradução contínua, não uso ocioso. Respostas válidas não medem precisão. Captura, OCR, interface e correspondências diretas do glossário são excluídos.

### Configurações iniciais de API

Os exemplos são conexões personalizadas comuns; não alteram o idioma da interface nem o destino. DeepSeek Flash suporta requisições sem raciocínio; o Google inclui tradução nos usos do Gemini 3.5 Flash-Lite. Fontes: [DeepSeek](https://api-docs.deepseek.com/), [modelo Gemini](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite), [usos e preços Gemini](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite).

| Configuração | DeepSeek | Google Gemini |
|---|---|---|
| URL base | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| Modelo | `deepseek-flash` | `gemini-3.5-flash-lite` |
| Chave de API | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| Parâmetros adicionais | `{"thinking":{"type":"disabled"}}` | `{}` (padrões do modelo) |

Adicione uma conexão em **API e uso**, escolha **OpenAI Chat Completions** (`openai_chat`), autenticação **Auto** e deixe **URL completa** desativada. Insira sua chave. Nas opções avançadas, use os parâmetros da tabela, deixe os cabeçalhos vazios e a consulta de saldo desativada. Salve e teste a conexão. Veja [configurações](docs/PROVIDERS.md) e [a API compatível com OpenAI do Google](https://aistudio.google.com/docs/openai).

Disponibilidade, preços, cotas e tratamento de dados são definidos pelos provedores: [preços DeepSeek](https://api-docs.deepseek.com/quick_start/pricing/), [preços e dados Gemini](https://aistudio.google.com/docs/pricing), [regiões Gemini](https://aistudio.google.com/docs/available-regions). Os níveis gratuito e pago do Gemini têm limites e condições de dados diferentes.

## Funcionamento

1. O **addon BianLingo** lê o chat pela API de addons e mostra brevemente pixels codificados em um canto do jogo.
2. O **aplicativo de desktop** decodifica os pixels, traduz pela conexão selecionada e exibe o resultado na sobreposição.

O aplicativo não lê a memória do jogo, não injeta código nem envia mensagens automaticamente. O OCR captura a região escolhida. Se a Blizzard ocultar o texto dos addons, a transmissão por pixels não poderá recuperá-lo; o OCR é outra entrada.

## Requisitos e idiomas

| Plataforma | Status |
|---|---|
| Windows 10/11 x64 | Disponível. OCR de janela requer Windows 10 1903 ou posterior. |
| macOS | Em desenvolvimento, previsto para uma versão futura; ainda não há uma compilação para macOS. |

- World of Warcraft com o addon BianLingo, em modo janela ou janela sem bordas.
- Modelo local ou conexão de API própria. O pacote completo inclui Bergamot inglês ↔ chinês simplificado; outros pares disponíveis são preparados separadamente.

A interface oferece inglês, chinês simplificado, japonês, coreano, francês, alemão, espanhol, português do Brasil e russo. Selecione **Idioma da interface** em **Aparência do chat** e salve para aplicar imediatamente. O padrão é **Seguir o sistema**, com inglês para idiomas não compatíveis; localidades chinesas usam chinês simplificado. O idioma da interface, o do README e os destinos de tradução são independentes. A cobertura depende do mecanismo e do modelo. Erros e notificações de status aparecem em chinês na interface chinesa e em inglês nos demais idiomas da interface.

O addon declara suporte a Classic Era e clientes Classic/Retail compatíveis. O acesso ao chat depende do cliente e da situação. HDR, monitores com DPI diferentes e versões do jogo precisam de verificação no ambiente correspondente.

## Início rápido

1. **Instalar o addon.** Copiar `AddOns/BianLingo` para `World of Warcraft/<client>/Interface/AddOns/`, por exemplo em `_retail_`, `_classic_era_` ou `_forever_`. Conferir `Interface/AddOns/BianLingo/BianLingo.toc`, ativar o addon no jogo e executar `/reload`.
2. **Iniciar e mover.** Manter `BianLingo.exe` junto de `data` e iniciar o EXE. **Clicar no gato no canto superior direito abre as configurações; arrastar o mesmo ícone move a janela.**
3. **Escolher a tradução.** Selecionar uma conexão nas configurações de API. **Bergamot / CTranslate2:** escolher o idioma de destino e preparar seu pacote, ou abrir o gerenciador para baixar pares antecipadamente. **Ollama:** escolher e baixar um modelo; não precisa de chave API. **API on-line:** escolher ou adicionar uma conexão, informar URL base, modelo e chave API, salvar e testar. A tabela acima contém exemplos.
4. **Configurar a recepção.** Escolher o idioma de destino do chat nas configurações de API e os canais no escopo de tradução; salvar. A tradução local exige um par compatível já preparado.
5. **Traduzir uma resposta.** O atalho padrão é a **tecla · / acento grave (&#96;), normalmente abaixo de Esc**. Digitar e pressionar **Enter** traduz e copia o resultado; colar manualmente no jogo. Na tradução de saída, escolher idioma e estilo. Para mudar o atalho, clicar no campo, pressionar a nova combinação e salvar. Os destinos de entrada e saída são independentes.
6. **Sincronizar o chat do jogo.** Configurar janelas e canais no WoW. Com o EXE aberto, executar `/bl windows` ou sincronizar em `/bl settings`. Após a recepção completa, as abas são **substituídas** automaticamente, sem outra importação. O addon precisa estar ativado.


Use o aplicativo e o addon do mesmo pacote. Para receber chat em chinês para outro idioma, execute `/bl cn off` se o filtro de chinês do addon estiver ativo.

### Windows SmartScreen

O Windows pode exibir “O Windows protegeu o computador” nesta versão sem assinatura. É a verificação de assinatura e reputação de downloads do Microsoft Defender SmartScreen. Arquivos novos ou não assinados podem ser desconhecidos; assinar não garante que os avisos desapareçam imediatamente. Consulte a explicação oficial da Microsoft. [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## Documentação

[Conexões](docs/PROVIDERS.md) · [Modelos locais](docs/LOCAL_TRANSLATION.md) · [Abas](docs/CHAT_TABS.md) · [Ações e bloqueio](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) · [Estilos](docs/SKINS.md) · [OCR](docs/OCR_FALLBACK.md) · [Compilação](docs/BUILDING.md)

Os guias detalhados e os avisos do projeto usam inglês por padrão. Os README traduzidos estão nos links acima.

## Privacidade e licença

A tradução local é executada no seu PC. A conexão on-line envia o texto ao provedor configurado, sujeito às condições e tarifas dele. Logs podem conter nomes de jogadores e mensagens privadas. Veja [Privacidade](PRIVACY.md).

O código usa a licença [MIT](LICENSE). Dependências, modelos e terminologia mantêm suas próprias condições e atribuições: [avisos de terceiros](THIRD_PARTY_NOTICES.md). World of Warcraft e Blizzard Entertainment são marcas de seus respectivos titulares.

Todos os downloads e recursos públicos estão disponíveis sem doação. Contato para dúvidas sobre o projeto, privacidade ou licenças: [@erensm1th](https://github.com/erensm1th).
