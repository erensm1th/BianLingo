<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [한국어](README.ko.md)<br>
**Français** · [Deutsch](README.de.md) · [Español](README.es.md) · [Português (Brasil)](README.pt-BR.md) · [Русский](README.ru.md)

**Traduction en temps réel du chat de World of Warcraft.** Lisez les messages dans la langue choisie et traduisez vos réponses dans une fenêtre superposée compacte.

[Compiler les sources](docs/BUILDING.md) · [Confidentialité](PRIVACY.md)

BianLingo est un projet de fans gratuit et non commercial. Il n’est ni affilié à Blizzard Entertainment, ni approuvé ou soutenu par Blizzard.

## Fonctionnalités

- **Traduction locale ou en ligne** : Bergamot, CTranslate2, Ollama ou connexion API avec URL, modèle et clé. Formats OpenAI compatibles, Anthropic et Gemini.
- **Modèles avant de jouer** : choisissez la langue cible et préparez les paires nécessaires. Les langues disponibles dépendent du moteur et du modèle.
- **Fenêtre de chat** : déplacement, redimensionnement, transparence du fond, taille du texte, thèmes et habillages personnalisés.
- **Onglets** : noms et filtres propres à chaque onglet ; import de la configuration du chat exportée par l’addon.
- **Réponse au clic** : clic gauche pour préparer une réponse sur le canal du message ; menu droit pour chuchoter, demander manuellement le niveau ou bloquer. Entrée traduit et copie ; vous collez et envoyez vous-même dans le jeu.
- **Filtres** : canaux, bruit et noms utilisant l’écriture de la langue cible. Protection des liens et termes du jeu ; consignes distinctes pour les messages reçus et les réponses.
- **Renvoi manuel** : cache mémoire de 10 messages complets par défaut, réglable de 1 à 100. Clic droit sur la minicarte ou `/bl replay` pour renvoyer ; les doublons ne sont pas retraduits.
- **Pixels et OCR** : matrice couleur compacte ou grille binaire, masquage à l’inactivité avec petit marqueur d’état. Sous Windows, OCR d’une zone de chat choisie.
- **Journaux facultatifs** : désactivés par défaut ; ajout quotidien en UTF-8 des messages reçus et des traductions de sortie soumises.
- **Données portables** : paramètres, journaux, modèles téléchargés et caches de l’application dans `UserData`, à côté de `BianLingo.exe`.

## Modes de traduction

| Moteur | Capacités | Contraintes | Usage |
|---|---|---|---|
| **API en ligne** | Consignes et styles, sans modèle local | Réseau, quotas, frais et conditions de traitement des données | Consignes sans inférence locale |
| **Ollama** | LLM local, consignes et styles | Téléchargement du moteur et du modèle ; mémoire et latence liées au matériel | Traitement local avec consignes |
| **Bergamot** | Traduction CPU hors ligne, sans frais API | Paires préparées ; pas de consignes | Paire hors ligne prise en charge |
| **CTranslate2** | Traduction CPU INT8 hors ligne, sans frais API | Paires OPUS-MT préparées ; pas de consignes | Paire OPUS-MT hors ligne |

Le routage automatique Bergamot/CTranslate2 suppose une source anglaise, ou chinoise détectée par les caractères Han lorsque la cible est l’anglais. Télécharger davantage de paires n’ajoute pas une détection universelle de la langue source. Le traitement local n’envoie pas le chat à un fournisseur de traduction. Résultats et latence varient selon le modèle, la paire, le message et le matériel/réseau.

### Benchmark · AMD Ryzen 7 9700X

| Solution | Appel médian | CPU à 1 message/s | Mémoire résidente locale |
|---|---:|---:|---:|
| Bergamot | 20.9 ms | 0.10% | 337.5 MiB |
| CTranslate2 · Ruy CPU INT8 | 67.2 ms | 1.36% | 157.3 MiB |
| DeepSeek · `deepseek-chat` | 708.2 ms | 0.01% | 23.4 MiB |

### Configurations API de départ

Ces connexions personnalisées ne changent ni la langue de l’interface ni la cible. DeepSeek Flash accepte les requêtes sans raisonnement ; Google cite la traduction parmi les usages de Gemini 3.5 Flash-Lite. Sources : [DeepSeek](https://api-docs.deepseek.com/), [modèle Gemini](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite), [usages et tarifs Gemini](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite).

| Paramètre | DeepSeek | Google Gemini |
|---|---|---|
| URL de base | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| Modèle | `deepseek-flash` | `gemini-3.5-flash-lite` |
| Clé API | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| Paramètres supplémentaires | `{"thinking":{"type":"disabled"}}` | `{}` (valeurs du modèle) |

Ajoutez une connexion dans **API et utilisation**, choisissez **OpenAI Chat Completions** (`openai_chat`), l’authentification **Auto**, puis désactivez **URL complète**. Entrez votre clé. Dans les options avancées, utilisez les paramètres du tableau, laissez les en-têtes vides et la consultation du solde désactivée. Enregistrez puis testez. Voir [les connexions](docs/PROVIDERS.md) et [l’API compatible OpenAI de Google](https://aistudio.google.com/docs/openai).

Disponibilité, prix, quotas et traitement des données relèvent des fournisseurs : [tarifs DeepSeek](https://api-docs.deepseek.com/quick_start/pricing/), [tarifs et données Gemini](https://aistudio.google.com/docs/pricing), [régions Gemini](https://aistudio.google.com/docs/available-regions). Les offres gratuites et payantes Gemini ont des limites et conditions de données différentes.

## Fonctionnement

1. L’**addon BianLingo** lit les messages par l’API des addons et affiche brièvement des pixels encodés dans un coin du jeu.
2. L’**application de bureau** décode ces pixels, traduit avec la connexion choisie et affiche le résultat.

L’application ne lit pas la mémoire du jeu, n’injecte pas de code et n’envoie pas automatiquement de messages. L’OCR capture la zone sélectionnée. Si Blizzard masque le texte aux addons, les pixels ne permettent pas de le récupérer ; l’OCR fournit une autre entrée.

## Prérequis et langues

| Plateforme | État |
|---|---|
| Windows 10/11 x64 | Disponible. OCR de fenêtre à partir de Windows 10 1903. |
| macOS | En développement, prévu pour une prochaine version ; aucun paquet macOS n’est encore disponible. |

- World of Warcraft avec l’addon BianLingo, en mode fenêtré ou sans bordure.
- Modèle local ou connexion API personnelle. Le paquet complet inclut Bergamot anglais ↔ chinois simplifié ; les autres paires disponibles se préparent séparément.

L’interface prend en charge l’anglais, le chinois simplifié, le japonais, le coréen, le français, l’allemand, l’espagnol, le portugais du Brésil et le russe. Choisissez la **Langue de l’interface** dans **Chat**, puis enregistrez pour l’appliquer immédiatement. La **Langue du système** est utilisée par défaut, avec repli vers l’anglais pour les langues non prises en charge ; les paramètres régionaux chinois utilisent le chinois simplifié. Les langues de l’interface, du README et les cibles de traduction sont indépendantes. La couverture dépend du moteur et du modèle. Les erreurs et notifications d’état sont en chinois dans l’interface chinoise et en anglais dans toutes les autres langues d’interface.

L’addon déclare la prise en charge de Classic Era et des clients Classic/Retail compatibles. L’accès au chat dépend du client et de la situation. HDR, écrans avec différents DPI et versions du jeu demandent une vérification dans l’environnement concerné.

## Démarrage rapide

1. **Installer l’addon.** Copier `AddOns/BianLingo` dans `World of Warcraft/<client>/Interface/AddOns/` (`_retail_`, `_classic_era_` ou `_forever_`, par exemple). Vérifier le fichier `Interface/AddOns/BianLingo/BianLingo.toc`, activer l’addon en jeu et exécuter `/reload`.
2. **Démarrer et déplacer.** Garder `BianLingo.exe` avec `data`, puis lancer l’EXE. **Cliquer sur le chat en haut à droite ouvre les réglages ; faire glisser cette même icône déplace la fenêtre.**
3. **Choisir la traduction.** Dans les réglages API, sélectionner une connexion. **Bergamot / CTranslate2 :** choisir la langue cible et préparer le pack courant, ou ouvrir la gestion des packs pour les télécharger à l’avance. **Ollama :** choisir et télécharger un modèle, sans clé API. **API en ligne :** choisir ou ajouter une connexion, renseigner l’URL de base, le modèle et la clé API, enregistrer et tester. Le tableau ci-dessus fournit des exemples.
4. **Configurer les messages reçus.** Choisir la langue cible du chat dans les réglages API et les canaux dans la portée de traduction, puis enregistrer. La traduction locale exige une paire de langues compatible et préparée.
5. **Traduire une réponse.** Le raccourci par défaut est la **touche · / accent grave (&#96;), généralement sous Échap**. Saisir le texte puis appuyer sur **Entrée** pour traduire et copier ; coller ensuite dans le jeu. Dans la traduction de sortie, choisir la langue et le style. Cliquer sur le champ du raccourci, saisir une nouvelle combinaison et enregistrer. Les langues de réception et de sortie sont indépendantes.
6. **Synchroniser les fenêtres du jeu.** Configurer les fenêtres et canaux dans WoW. Avec l’EXE ouvert, exécuter `/bl windows` ou choisir la synchronisation dans `/bl settings`. Une réception complète **remplace** automatiquement les onglets, sans autre importation. L’addon doit rester activé.


Utilisez l’application et l’addon du même paquet. Pour recevoir des messages chinois destinés à une autre langue, exécutez `/bl cn off` si le filtre chinois de l’addon est actif.

### Windows SmartScreen

Windows peut afficher « Windows a protégé votre ordinateur » pour cette version non signée. Il s’agit du contrôle de signature et de réputation des téléchargements de Microsoft Defender SmartScreen. Les fichiers nouveaux ou non signés peuvent être inconnus ; une signature ne garantit pas la disparition immédiate des alertes. Consultez l’explication officielle de Microsoft. [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## Documentation

[Connexions](docs/PROVIDERS.md) · [Modèles locaux](docs/LOCAL_TRANSLATION.md) · [Onglets](docs/CHAT_TABS.md) · [Actions et blocage](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) · [Habillages](docs/SKINS.md) · [OCR](docs/OCR_FALLBACK.md) · [Compilation](docs/BUILDING.md)

Les guides détaillés et les notices du projet sont en anglais par défaut. Les README traduits sont accessibles en haut de page.

## Confidentialité et licence

La traduction locale fonctionne sur votre PC. Une connexion en ligne transmet le texte au fournisseur configuré, selon ses conditions et tarifs. Les journaux peuvent contenir des noms de joueurs et des messages privés. Voir [Confidentialité](PRIVACY.md).

Le code du projet est sous licence [MIT](LICENSE). Dépendances, modèles et terminologie conservent leurs propres conditions et attributions : [mentions tierces](THIRD_PARTY_NOTICES.md). Les marques World of Warcraft et Blizzard Entertainment appartiennent à leurs titulaires.

Tous les téléchargements et fonctionnalités publics sont accessibles sans don. Contact pour les questions sur le projet, la confidentialité ou les licences : [@erensm1th](https://github.com/erensm1th).
