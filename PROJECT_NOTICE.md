# Project notice

Updated: 2026-11-04

## About BianLingo

BianLingo is a free, non-commercial World of Warcraft fan project. Current downloads and platform support are listed in the README and release pages.

BianLingo is not affiliated with Blizzard Entertainment. BianLingo is not made, reviewed, approved, supported or endorsed by Blizzard. Warcraft, World of Warcraft and Blizzard Entertainment are trademarks or registered trademarks of their respective owners. References to games, models, services and libraries identify compatibility and sources, not endorsement.

The hand-drawn two-cat logo is original artwork created for BianLingo. Third-party code and assets retain their own ownership and terms. The package includes no Blizzard game textures or commercial game fonts. See [Artwork provenance](resources/ASSET_PROVENANCE.md).

## License

BianLingo's original code is available under the [MIT License](LICENSE). MIT permits use, modification, commercial use and redistribution under its conditions. The project’s non-commercial operation adds no restrictions to that license.

Third-party code, models, runtimes and terminology keep their original licenses. Free distribution and voluntary support do not waive attribution, notice preservation or applicable source-availability requirements. See [Third-party notices](THIRD_PARTY_NOTICES.md) and [Corresponding source](SOURCE_ACCESS.md).

Unresolved source reviews and known limitations are recorded in `RELEASE_REVIEW.json` in the package, or `resources/release-review.json` in the source tree. See [Release compliance](docs/RELEASE_COMPLIANCE.md) for the checks used before distribution. Those checks do not grant rights beyond the applicable licenses.

## Voluntary support

Voluntary support helps fund maintenance, testing and documentation. Support is optional project funding, not charitable fundraising, and Charitable donation receipts are not provided.

- All public downloads, updates and features are available without donating. Contributions do not buy activation keys, exclusive builds, early access, API credits or service guarantees.
- Usage is not limited by contribution amount, and the app includes no donation prompts. Support does not guarantee a particular fix, release date or continued maintenance.
- Any support links will be on the project website or distribution page. I keep donation requests, QR codes and advertisements out of the addon, game chat and translation overlay. BianLingo has no built-in payment feature.
- Optional API fees are paid directly to your provider under its terms and are unrelated to project support. Local models and runtimes also have their own terms.
- Payment, cancellation and refund handling follow the support channel's rules and applicable law. Statutory rights remain unaffected.

## Game interaction and limitations

The Lua addon reads available chat through game-provided APIs and displays encoded pixels. The desktop app decodes those pixels, or uses optional OCR on a selected visible chat region. Game restrictions, client updates and capture conditions can make messages unavailable or cause missed recognition.

On Windows, BianLingo finds the game through OS window APIs. It does not open a game-process handle, query the game's executable path, read/write game memory, inject code or automatically paste/send chat. When you request a reply, it can copy text and restore game focus; you paste and send the message yourself.

The blocklist is stored in application data. You copy a data-only sync code into the addon, and WoW saves the list through SavedVariables. This does not read/write game-directory, addon Lua or WTF files, and needs no game path. Install the addon manually.

Compatibility with every client, access to restricted chat and account safety cannot be guaranteed. Screen capture is not Blizzard authorization. Use BianLingo in accordance with the applicable game terms, including the [UI Add-On Development Policy](https://us.forums.blizzard.com/en/wow/t/ui-add-on-development-policy/24534) and [Blizzard End User License Agreement](https://www.blizzard.com/en-us/legal/08b946df-660a-40e4-a072-1fbde65173b1/blizzard-end-user-license-agreement) for your region and service.

Terminology matching, OCR and model translations can be wrong. Check results before sending messages, trading or making decisions based on a translation. The software is provided under the applicable licenses' as-is and no-warranty terms, subject to liability that cannot lawfully be excluded. See [Privacy](PRIVACY.md) for data handling and log controls.

## Contact

For project, privacy, copyright or licensing questions, use [GitHub Issues](https://github.com/erensm1th/BianLingo/issues). Include the version and a short description of the issue. Please keep API keys, private chats and account credentials out of public reports.
