<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [한국어](README.ko.md)<br>
[Français](README.fr.md) · [Deutsch](README.de.md) · **Español** · [Português (Brasil)](README.pt-BR.md) · [Русский](README.ru.md)

**Traducción en tiempo real del chat de World of Warcraft.** Lee los mensajes en el idioma elegido y traduce tus respuestas en una ventana superpuesta compacta.

[Compilar el código](docs/BUILDING.md) · [Privacidad](PRIVACY.md)

BianLingo es un proyecto de fans gratuito y no comercial. No está afiliado a Blizzard Entertainment ni cuenta con su aprobación o respaldo.

## Funciones

- **Traducción local o en línea**: Bergamot, CTranslate2, Ollama o una conexión con URL, modelo y clave API propios. Admite formatos compatibles con OpenAI, Anthropic y Gemini.
- **Modelos antes de jugar**: elige el idioma de destino y prepara los pares necesarios. Los idiomas disponibles dependen del motor y del modelo.
- **Ventana de chat**: mueve y redimensiona la ventana, ajusta transparencia del fondo y tamaño de texto, y elige temas o diseños personalizados.
- **Pestañas de chat**: nombres y filtros independientes, con importación de la configuración exportada por el addon.
- **Responder con un clic**: el clic izquierdo prepara una respuesta en el canal del mensaje. El menú derecho permite susurrar, consultar manualmente el nivel o bloquear. Intro traduce y copia; tú pegas y envías en el juego.
- **Filtros**: canales, ruido y nombres que usan la escritura del idioma de destino. Protección de enlaces y términos del juego; instrucciones separadas para mensajes recibidos y respuestas.
- **Reenvío manual**: caché en memoria de 10 mensajes completos por defecto, configurable entre 1 y 100. Clic derecho en el minimapa o `/bl replay` para reenviar; los duplicados no vuelven a traducirse.
- **Píxeles y OCR**: matriz de color compacta o cuadrícula binaria, con ocultación en reposo y pequeño indicador de estado. En Windows se puede leer por OCR una zona de chat seleccionada.
- **Registros opcionales**: desactivados por defecto; añaden a archivos diarios UTF-8 los mensajes recibidos y las traducciones de salida confirmadas.
- **Datos portátiles**: ajustes, registros, modelos descargados y cachés de la aplicación en `UserData`, junto a `BianLingo.exe`.

## Opciones de traducción

| Motor | Capacidades | Límites | Uso |
|---|---|---|---|
| **API en línea** | Instrucciones y estilos, sin descargar modelos | Red, cuotas, tarifas y políticas de datos del proveedor | Instrucciones sin inferencia local |
| **Ollama** | LLM local con instrucciones y estilos | Descarga de entorno y modelo; memoria y latencia según hardware | Procesamiento local con instrucciones |
| **Bergamot** | Traducción CPU sin conexión ni tarifas API | Pares preparados; sin instrucciones | Par compatible sin conexión |
| **CTranslate2** | Traducción CPU INT8 sin conexión ni tarifas API | Pares OPUS-MT preparados; sin instrucciones | Par OPUS-MT sin conexión |

El enrutamiento automático de Bergamot/CTranslate2 supone texto en inglés, o selecciona chino mediante caracteres Han cuando el destino es inglés. Descargar más pares no añade detección automática de cualquier idioma de origen. El procesamiento local no envía el chat a un proveedor de traducción. La latencia y los resultados dependen del modelo, par, mensaje y condiciones del hardware o la red.

### Benchmark · AMD Ryzen 7 9700X

| Método | Mediana por llamada | CPU a 1 mensaje/s | Memoria residente local |
|---|---:|---:|---:|
| Bergamot | 20.9 ms | 0.10% | 337.5 MiB |
| CTranslate2 · Ruy CPU INT8 | 67.2 ms | 1.36% | 157.3 MiB |
| DeepSeek · `deepseek-chat` | 708.2 ms | 0.01% | 23.4 MiB |

### Configuración inicial de API

Estos ejemplos son conexiones personalizadas normales y no cambian el idioma de la interfaz ni el destino. DeepSeek Flash permite solicitudes sin razonamiento; Google incluye la traducción entre los usos de Gemini 3.5 Flash-Lite. Documentación: [DeepSeek](https://api-docs.deepseek.com/), [modelo Gemini](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite), [usos y precios Gemini](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite).

| Ajuste | DeepSeek | Google Gemini |
|---|---|---|
| URL base | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| Modelo | `deepseek-flash` | `gemini-3.5-flash-lite` |
| Clave API | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| Parámetros adicionales | `{"thinking":{"type":"disabled"}}` | `{}` (valores del modelo) |

Añade una conexión en **API y uso**. Selecciona **OpenAI Chat Completions** (`openai_chat`), autenticación **Auto** y deja **URL completa** desactivada. Introduce tu clave. En los ajustes avanzados aplica los parámetros de la tabla, deja vacías las cabeceras y desactiva la consulta de saldo. Guarda y prueba la conexión. Consulta [los ajustes](docs/PROVIDERS.md) y [la API compatible con OpenAI de Google](https://aistudio.google.com/docs/openai).

Disponibilidad, precios, cuotas y tratamiento de datos dependen del proveedor: [precios DeepSeek](https://api-docs.deepseek.com/quick_start/pricing/), [precios y datos Gemini](https://aistudio.google.com/docs/pricing), [regiones Gemini](https://aistudio.google.com/docs/available-regions). Los niveles gratuitos y de pago de Gemini tienen límites y condiciones de datos diferentes.

## Funcionamiento

1. El **addon BianLingo** lee el chat mediante la API de addons y muestra brevemente píxeles codificados en una esquina del juego.
2. La **aplicación de escritorio** decodifica los píxeles, traduce mediante la conexión seleccionada y muestra el resultado.

La aplicación no lee la memoria del juego, no inyecta código ni envía chat automáticamente. OCR captura la región elegida. Si Blizzard oculta el texto a los addons, la transmisión por píxeles no puede recuperarlo; OCR es otra vía de entrada.

## Requisitos e idiomas

| Plataforma | Estado |
|---|---|
| Windows 10/11 x64 | Disponible. La captura OCR requiere Windows 10 1903 o posterior. |
| macOS | En desarrollo, previsto para una versión futura; aún no hay una compilación para macOS. |

- World of Warcraft con el addon BianLingo, en ventana o ventana sin bordes.
- Modelo local o conexión API propia. El paquete completo incluye Bergamot inglés ↔ chino simplificado; los demás pares disponibles se preparan aparte.

La interfaz admite inglés, chino simplificado, japonés, coreano, francés, alemán, español, portugués de Brasil y ruso. Selecciona **Idioma de la interfaz** en **Chat** y guarda para aplicarlo de inmediato. El valor predeterminado es **Idioma del sistema**, con inglés para idiomas no compatibles; las configuraciones regionales chinas usan chino simplificado. El idioma de la interfaz, el del README y los destinos de traducción son independientes. La cobertura depende del motor y del modelo. Los errores y avisos de estado se muestran en chino en la interfaz china y en inglés en todos los demás idiomas de interfaz.

El addon declara compatibilidad con Classic Era y clientes Classic/Retail compatibles. La disponibilidad del chat depende del cliente y la situación. HDR, monitores con DPI distintos y versiones del juego requieren comprobación en cada entorno.

## Inicio rápido

1. **Instalar el addon.** Copiar `AddOns/BianLingo` a `World of Warcraft/<client>/Interface/AddOns/`, por ejemplo en `_retail_`, `_classic_era_` o `_forever_`. Comprobar `Interface/AddOns/BianLingo/BianLingo.toc`, activar el addon en el juego y ejecutar `/reload`.
2. **Iniciar y mover.** Mantener `BianLingo.exe` junto a `data` y ejecutar el EXE. **Pulsar el gato de la esquina superior derecha abre los ajustes; arrastrar el mismo icono mueve la ventana.**
3. **Elegir traducción.** Seleccionar una conexión en los ajustes API. **Bergamot / CTranslate2:** elegir el idioma de destino y preparar su paquete, o abrir el gestor para descargar pares antes de jugar. **Ollama:** elegir y descargar un modelo, sin clave API. **API en línea:** elegir o añadir una conexión, introducir URL base, modelo y clave API, guardar y probar. Hay ejemplos en la tabla anterior.
4. **Configurar la recepción.** Elegir el idioma de destino del chat en los ajustes API y los canales en el alcance de traducción; guardar. La traducción local requiere un par compatible ya preparado.
5. **Traducir una respuesta.** El atajo predeterminado es la **tecla · / acento grave (&#96;), normalmente debajo de Esc**. Escribir y pulsar **Enter** para traducir y copiar; pegar manualmente en el juego. En traducción de salida se eligen idioma y estilo. Para cambiar el atajo, pulsar su campo, introducir la nueva combinación y guardar. Los destinos de recepción y salida son independientes.
6. **Sincronizar el chat del juego.** Configurar ventanas y canales en WoW. Con el EXE abierto, ejecutar `/bl windows` o sincronizar desde `/bl settings`. Al recibir la configuración completa, las pestañas se **sustituyen** automáticamente, sin otra importación. Mantener el addon activado.


Usa aplicación y addon del mismo paquete. Para recibir mensajes chinos que se traducirán a otro idioma, ejecuta `/bl cn off` si está activo el filtro chino del addon.

### Windows SmartScreen

Windows puede mostrar «Windows protegió su PC» para esta versión sin firma. Es la comprobación de firma y reputación de descargas de Microsoft Defender SmartScreen. Los archivos nuevos o sin firma pueden ser desconocidos; firmarlos no garantiza que los avisos desaparezcan inmediatamente. Consulta la explicación oficial de Microsoft. [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## Documentación

[Conexiones](docs/PROVIDERS.md) · [Modelos locales](docs/LOCAL_TRANSLATION.md) · [Pestañas](docs/CHAT_TABS.md) · [Acciones y bloqueo](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) · [Diseños](docs/SKINS.md) · [OCR](docs/OCR_FALLBACK.md) · [Compilación](docs/BUILDING.md)

Las guías detalladas y los avisos del proyecto están en inglés de forma predeterminada. Los README traducidos están enlazados arriba.

## Privacidad y licencia

La traducción local se realiza en tu PC. Una conexión en línea envía el texto al proveedor configurado y aplica sus condiciones y tarifas. Los registros pueden incluir nombres y mensajes privados. Consulta [Privacidad](PRIVACY.md).

El código del proyecto usa [MIT](LICENSE). Dependencias, modelos y terminología conservan sus propias condiciones y atribuciones: [avisos de terceros](THIRD_PARTY_NOTICES.md). Las marcas World of Warcraft y Blizzard Entertainment pertenecen a sus respectivos titulares.

Todas las descargas y funciones públicas están disponibles sin donaciones. Contacto para consultas sobre el proyecto, privacidad o licencias: [@erensm1th](https://github.com/erensm1th).
