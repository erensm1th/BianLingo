"""Shared blocklist roundtrip and actual sender tests; no running game or network."""
import argparse
import json
import zlib
from pathlib import Path
from bridge_lua_tests import boot


def run(source, output):
    sender = boot(home_realm="Westfall")
    b = sender.g.BLGBlocklist
    db = sender.g.BianLingoDB
    code = Path(source).read_bytes()
    b.ImportCode(db, code)
    b.error = b'old storage warning'
    b.ImportCode(db, code)
    assert b.error is None, 'Successful import kept a stale warning'
    assert b.IsBlocked(db, b"Alice-Westfall", b"")
    assert b.IsBlocked(db, "玩家-比格沃斯".encode(), b"")
    sender.packets = []
    queries = sender.g.queries
    sender.chat("need healer for BRD", "Alice-Westfall")
    sender.chat("need healer for BRD", "Alice")
    sender.drain()
    assert sender.g.queries == queries, "Blocked chat performed a player lookup"
    assert not sender.packets, "Blocked chat reached the pixel queue"
    sender.chat("need healer for BRD", "Alice-Other")
    sender.drain()
    assert sender.g.queries > queries, "Cross-realm namesake was wrongly blocked"
    b.Set(db, b"Alice-Westfall", False)
    b.Set(db, b"Bob-Other", True)
    for i in range(70): b.Set(db, f"Player{i}-Realm".encode(), True)
    queries = sender.g.queries
    sender.packets = []
    sender.command("blocksync")
    sender.tick()
    sender.chat("realtime message has priority", "Live-Other", "Player-live")
    sender.drain()
    assert sender.g.queries == queries + 1, "Synchronization queried player information"
    controls = [p["raw"] for p in sender.packets if p["event"] == 23]
    assert controls and any(p["event"] == 1 for p in sender.packets), "Controls and live chat did not coexist"
    assert not b.IsBlocked(db, b"Alice-Westfall", b"")
    b.ImportCode(db, code)  # Stale manual paste must not undo a newer removal.
    assert not b.IsBlocked(db, b"Alice-Westfall", b""), "Stale import resurrected a removed block"
    original = json.loads(Path(source).with_suffix(".json").read_text(encoding="utf8"))
    Path(output).write_text(json.dumps({"packets": controls, "original": original}), encoding="utf8")
    # Drive the actual addon panel callbacks, including edits during an active sync.
    sender.command("blocks")
    sender.g.BianLingoBridgeBlockName[b"text"] = b"Panel-Quel'Thalas"
    sender.g.BianLingoBridgeBlockAdd[b"scripts"][b"OnClick"]()
    assert b.IsBlocked(db, b"Panel", b"Quel'Thalas")
    sender.g.BianLingoBridgeBlockRemove[b"scripts"][b"OnClick"]()
    assert not b.IsBlocked(db, b"Panel-Quel'Thalas", b"")
    b.Set(db, b"Latest-Realm", True)
    sender.command("blocksync")
    sender.command("blocksync")
    sender.packets = []
    sender.drain()
    assembled = {}
    for packet in sender.packets:
        if packet["event"] == 23:
            assembled.setdefault(packet["seq"], {})[packet["part"]] = packet["body"]
    snapshots = [json.loads(b"".join(parts[k] for k in sorted(parts))[1:]) for parts in assembled.values()]
    assert any(any(row[0] == "Latest-Realm" and row[3] for row in state["entries"]) for state in snapshots), \
        "An edit during an active sync was not exported by the queued sync"
    assert not b.IsBlocked(db, b"Panel-Quel'Thalas", b""), "Panel removal did not remain in SavedVariables"
    # Exercise the actual paste/apply button, not an executable Lua sync file.
    sender.g.BianLingoBridgeBlockImportOpen[b'scripts'][b'OnClick']()
    sender.g.BianLingoBridgeBlockImportText[b'text'] = code.replace(b'\n', b'\r\n')
    sender.g.BianLingoBridgeBlockImportApply[b'scripts'][b'OnClick']()
    assert not b.IsBlocked(db, b"Alice-Westfall", b""), "UI import resurrected stale state"
    # A new Lua session loads the game's SavedVariables, without an external file.
    snapshot = {b'schemaVersion':1, b'entries':[[r[j] for j in range(1,5)] for _,r in b.Snapshot(db)[b'entries'].items()]}
    restarted = boot(saved={b'blocklistState': snapshot, b'blocklistNode': db[b'blocklistNode']})
    assert restarted.g.BLGBlocklist.IsBlocked(restarted.g.BianLingoDB, b'Bob-Other', b'')
    def raw_code(body, count):
        return f'BLGB1|{zlib.adler32(body):08x}|{count}\n'.encode() + body
    before = len(b.Snapshot(db)[b'entries'])
    malicious = [b'/run error("executed")', code[:-1], code.replace(b'BLGB1', b'BLGB9', 1),
        raw_code(b'ff\t1\tnode\t1\n',1), # invalid UTF-8
        raw_code(b'416c696365\t9007199254740992\tnode\t1\n',1),
        raw_code(b'416c696365\t1\tnode\t1\n416c696365\t2\tnode\t0\n',2),
        raw_code(b'416c696365\t1\tnode\t1\n',2), b'A' * 262145]
    protected = sender.lua.eval(b'function(db, code) return pcall(BLGBlocklist.ImportCode, db, code) end')
    for bad in malicious:
        result = protected(db,bad)
        assert result[0] is False, 'Malformed or executable input was accepted'
        assert len(b.Snapshot(db)[b'entries']) == before, 'Failed import mutated the list'
    # Full-size 512-entry snapshot, including UTF-8 and the largest safe counter.
    body = b''.join(f'{("Full"+str(i)+"-Realm").encode().hex()}\t9007199254740991\tnode\t1\n'.encode() for i in range(512))
    full = boot(); full.g.BLGBlocklist.ImportCode(full.g.BianLingoDB, raw_code(body,512))
    assert len(full.g.BLGBlocklist.Snapshot(full.g.BianLingoDB)[b'entries']) == 512
    # Failed merges must not partially apply even when the first row is valid.
    sender.lua.execute(b'''local before = #BLGBlocklist.Snapshot(BianLingoDB).entries
      local ok = pcall(BLGBlocklist.Merge, BianLingoDB, {schemaVersion=1,entries={
        {"Valid-Realm", 1, "node", true}, {"bad;name", 1, "node", true}}})
      assert(not ok and #BLGBlocklist.Snapshot(BianLingoDB).entries == before)''')
    print("PASS real Lua: checked paste, SavedVariables reload, early block, 512 entries, malformed rejection, queued sync and live priority")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    run(args.source, args.output)
