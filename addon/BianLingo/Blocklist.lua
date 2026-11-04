-- Local translation blocklist only; never calls Blizzard's account ignore APIs.
BLGBlocklist = {}
local B = BLGBlocklist
local current, records, clock, node = nil, {}, 0, ""
local active = false
local MAX = 512
B.MAX_IMPORT_BYTES = 262144
local function Trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
local function ValidUtf8(s)
    local i = 1
    while i <= #s do
        local a = s:byte(i)
        local n = a < 128 and 1 or a >= 194 and a <= 223 and 2 or
            a >= 224 and a <= 239 and 3 or a >= 240 and a <= 244 and 4 or 0
        if n == 0 or i + n - 1 > #s then return false end
        for j = i + 1, i + n - 1 do local b = s:byte(j); if b < 128 or b > 191 then return false end end
        local b = s:byte(i + 1)
        if n == 3 and ((a == 224 and b < 160) or (a == 237 and b > 159)) then return false end
        if n == 4 and ((a == 240 and b < 144) or (a == 244 and b > 143)) then return false end
        i = i + n
    end
    return true
end
function B.Key(raw)
    if type(raw) ~= "string" then error("Invalid player name") end
    local name = Trim(raw)
    if not ValidUtf8(name) then error("Invalid UTF-8 player name") end
    if #name == 0 or #name > 192 then error("Invalid player name") end
    local forbidden = "/\\|;\"{}[]=,:"
    for i = 1, #name do
        local byte = name:byte(i)
        if byte < 32 or byte == 127 or forbidden:find(name:sub(i, i), 1, true) then error("Invalid player name") end
    end
    local player, realm = name:match("^([^-]+)%-(.*)$")
    player = Trim(player or name)
    if player == "" or player:find(" ", 1, true) then error("Invalid player name") end
    if realm then
        realm = realm:gsub("[%s%-]", "")
        if realm == "" then error("Invalid realm") end
    end
    return (player .. (realm and "-" .. realm or "")):lower(), name
end
local function ValidNode(value)
    return type(value) == "string" and #value > 0 and #value <= 64 and not value:find("[^a-zA-Z0-9%-]")
end
function B.Snapshot(db)
    B.Ensure(db)
    local keys, entries = {}, {}
    for key in pairs(records) do keys[#keys + 1] = key end
    table.sort(keys)
    for _, key in ipairs(keys) do
        local r = records[key]
        entries[#entries + 1] = {r[1], r[2], r[3], r[4]}
    end
    return {schemaVersion = 1, entries = entries}
end
function B.Merge(db, state)
    B.Ensure(db)
    if type(state) ~= "table" or state.schemaVersion ~= 1 or type(state.entries) ~= "table" or #state.entries > MAX then
        error("Invalid blocklist")
    end
    local candidate, count, nextClock, changed = {}, 0, clock, false
    for key, r in pairs(records) do candidate[key] = r; count = count + 1 end
    for _, r in ipairs(state.entries) do
        if type(r) ~= "table" or #r ~= 4 or type(r[2]) ~= "number" or r[2] < 1 or r[2] > 9007199254740991
            or r[2] ~= math.floor(r[2]) or not ValidNode(r[3]) or type(r[4]) ~= "boolean" then error("Invalid block record") end
        local key, name = B.Key(r[1])
        local old = candidate[key]
        if not old or r[2] > old[2] or (r[2] == old[2] and
            (r[3] > old[3] or (r[3] == old[3] and not r[4] and old[4]))) then
            if not old then count = count + 1 end
            candidate[key] = {name, r[2], r[3], r[4]}; changed = true
        end
        nextClock = math.max(nextClock, r[2])
    end
    if count > MAX then error("Blocklist record limit reached (including removals)") end
    records, clock = candidate, nextClock
    active = false
    for _, r in pairs(records) do if r[4] then active = true; break end end
    if changed then db.blocklistState = B.Snapshot(db) end
    return changed
end
function B.Ensure(db)
    if current == db then return end
    current, records, clock = db, {}, 0
    active = false
    B.error = nil
    node = ValidNode(db.blocklistNode) and db.blocklistNode or
        ("game-" .. GetServerTime() .. "-" .. math.floor(GetTime() * 1000) .. "-" .. math.random(100000, 999999))
    db.blocklistNode = node
    if db.blocklistState then
        local ok, err = pcall(B.Merge, db, db.blocklistState)
        if not ok then B.error = tostring(err) end
    end
end
-- Data-only interchange. Never executes pasted text, writes files or reads clipboard.
function B.ImportCode(db, raw)
    if type(raw) ~= "string" or #raw > B.MAX_IMPORT_BYTES then error(BLGText("The sync code is invalid or too large")) end
    local code = raw:gsub("\r\n", "\n")
    local sum, countText, body = code:match("^BLGB1|([0-9a-f]+)|(%d+)\n(.*)$")
    if not sum or #sum ~= 8 or #countText > 3 then error(BLGText("Invalid sync code format")) end
    local count = tonumber(countText)
    if count > MAX then error(BLGText("The blocklist exceeds 512 records")) end
    local a, b = 1, 0
    for i = 1, #body do a = (a + body:byte(i)) % 65521; b = (b + a) % 65521 end
    if string.format("%08x", b * 65536 + a) ~= sum then error(BLGText("Sync code verification failed; copy the complete code again")) end
    local entries, seen, pos = {}, {}, 1
    while pos <= #body do
        if #entries >= count then error(BLGText("The sync code record count does not match")) end
        local ending = body:find("\n", pos, true)
        if not ending then error(BLGText("The sync code is incomplete")) end
        local line = body:sub(pos, ending - 1)
        local nameHex, versionText, origin, state = line:match("^([0-9a-f]+)\t(%d+)\t([a-zA-Z0-9%-]+)\t([01])$")
        if not nameHex or #nameHex > 384 or #nameHex % 2 ~= 0 or #versionText > 16 or not ValidNode(origin) then
            error(BLGText("Invalid sync code record"))
        end
        local name = nameHex:gsub("..", function(pair) return string.char(tonumber(pair, 16)) end)
        local key = B.Key(name)
        if seen[key] then error(BLGText("The sync code contains duplicate names")) end
        seen[key] = true
        local version = tonumber(versionText)
        if version < 1 or version > 9007199254740991 then error(BLGText("Invalid sync code version")) end
        entries[#entries + 1] = {name, version, origin, state == "1"}
        pos = ending + 1
    end
    if #entries ~= count then error(BLGText("The sync code record count does not match")) end
    local changed = B.Merge(db, {schemaVersion = 1, entries = entries})
    B.error = nil
    return changed, count
end
function B.Set(db, name, blocked)
    B.Ensure(db)
    local key, display = B.Key(name)
    if records[key] and records[key][4] == blocked then return false end
    if clock >= 9007199254740991 then error("Blocklist version limit reached") end
    local nextClock = math.max(clock, math.floor(GetServerTime() * 1000)) + 1
    return B.Merge(db, {schemaVersion = 1, entries = {{display, nextClock, node, not not blocked}}})
end
function B.Names(db)
    local names = {}
    for _, r in ipairs(B.Snapshot(db).entries) do if r[4] then names[#names + 1] = r[1] end end
    return names
end
function B.IsBlocked(db, name, realm)
    B.Ensure(db)
    if not active then return false end
    if not name or name == "" then return false end
    local ok, key = pcall(B.Key, name)
    if not ok then return false end
    local base = key:match("^([^-]+)")
    if records[base] and records[base][4] then return true end
    if not key:find("-", 1, true) and realm and realm ~= "" then
        local valid, full = pcall(B.Key, name .. "-" .. realm)
        if valid then key = full end
    end
    return records[key] and records[key][4] or false
end
