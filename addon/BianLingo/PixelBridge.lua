-- One-way, visible-screen transport. Never exports secret chat payloads.
local COLS, ROWS, CELL, CAPACITY = 96, 12, 2, 368
local SYNC = "111100001010110011001010001111010101001110000101110010100110101011110000110101001001011000111001"
local frame = CreateFrame("Frame", "BianLingoBridgeFrame", UIParent)
frame:SetFrameStrata("TOOLTIP")
frame:SetFrameLevel(100)
frame:EnableMouse(false)
local bg = frame:CreateTexture(nil, "BACKGROUND")
bg:SetAllPoints(); bg:SetColorTexture(0, 0, 0, 1)
local cells, values = {}, {}
local function EnsureCells(count)
for i = #cells + 1, count do
    local tex = frame:CreateTexture(nil, "ARTWORK")
    if tex.SetSnapToPixelGrid then tex:SetSnapToPixelGrid(false) end
    if tex.SetTexelSnappingBias then tex:SetTexelSnappingBias(0) end
    tex:SetColorTexture(0, 0, 0, 1)
    tex:Hide()
    cells[i] = tex
end
end
local marker = CreateFrame("Frame", "BianLingoIdleMarker", UIParent)
marker:SetFrameStrata("TOOLTIP"); marker:SetFrameLevel(101); marker:EnableMouse(false)
local markerCells, lastMarker = {}, nil
for i = 1, 16 do
    local tex = marker:CreateTexture(nil, "ARTWORK")
    if tex.SetSnapToPixelGrid then tex:SetSnapToPixelGrid(false) end
    if tex.SetTexelSnappingBias then tex:SetTexelSnappingBias(0) end
    tex:SetSize(1, 1); tex:SetPoint("TOPLEFT", (i - 1) % 4, -math.floor((i - 1) / 4))
    markerCells[i] = tex
end
marker:Hide()
local session, seq = math.floor(GetTime() * 10), 0
local enabled, transmitting = true, false
local chatState = 0 -- 0 unknown, 1 readable, 2 restricted (never stores secret text)
local UpdateMarker, UpdatePanel, PaintHeader, Draw
local compact, lastPacket = false, nil
local function Config()
    BianLingoDB = BianLingoDB or {}
    local db = BianLingoDB
    if type(db.enabled) ~= "boolean" then db.enabled = true end
    if BLGBlocklist then BLGBlocklist.Ensure(db) end
    local n = tonumber(db.cacheLimit)
    db.cacheLimit = n and n == math.floor(n) and n >= 1 and n <= 100 and n or 10
    if db.idleHide == nil then db.idleHide = true end
    if db.mode == "compact" and not db.compactPixelV2 then db.cell = 1; db.compactPixelV2 = true end
    if db.cell ~= 2 and db.cell ~= 3 and db.cell ~= 4 and not (db.mode == "compact" and db.cell == 1) then db.cell = 2 end
    if db.mode ~= "native" and db.mode ~= "compact" then db.mode = "native" end
    if not db.minimapButtonV2 then
        db.showReplayButton = true; db.minimapButtonV2 = true
        db.replayButtonX, db.replayButtonY = nil, nil
    end
    local angle = tonumber(db.minimapAngle)
    db.minimapAngle = angle and angle == angle and angle > -math.huge and angle < math.huge and angle % 360 or 225
    return db
end
local function Layout()
    local config = Config()
    enabled = config.enabled
    CELL = config.cell or 2
    compact = config.mode == "compact"
    COLS, ROWS = compact and 40 or 96, compact and 10 or 12
    EnsureCells(COLS * ROWS)
    local held = lastPacket
    lastPacket = nil; values = {}
    local _, height = GetPhysicalScreenSize()
    frame:SetScale((768 / height) / UIParent:GetEffectiveScale())
    frame:SetSize((COLS + 2) * CELL, (ROWS + 2) * CELL)
    marker:SetScale(frame:GetScale()); marker:SetSize(4, 4)
    local corners = {tl = "TOPLEFT", tr = "TOPRIGHT", bl = "BOTTOMLEFT", br = "BOTTOMRIGHT"}
    local corner = config.corner or "tl"
    local anchor = corners[corner] or "TOPLEFT"
    frame:ClearAllPoints()
    marker:ClearAllPoints()
    marker:SetPoint(anchor, UIParent, anchor, corner:sub(2, 2) == "r" and -1 or 1,
        corner:sub(1, 1) == "b" and 1 or -1)
    frame:SetPoint(anchor, UIParent, anchor, corner:sub(2, 2) == "r" and -6 or 6,
        corner:sub(1, 1) == "b" and 6 or -6)
    for i, tex in ipairs(cells) do
        tex:Hide()
        if i <= COLS * ROWS then
        tex:SetSize(CELL, CELL)
        tex:ClearAllPoints()
        tex:SetPoint("TOPLEFT", (1 + (i - 1) % COLS) * CELL,
            -(1 + math.floor((i - 1) / COLS)) * CELL)
        end
    end
    if PaintHeader then PaintHeader() end
    if Draw and held then Draw(held) end
    if UpdateMarker then UpdateMarker() end
    if not enabled then frame:Hide(); marker:Hide() end
end
Layout()
local function Pixel(i, value)
    if values[i] ~= value then
        values[i] = value
        if value == 0 then
            cells[i]:Hide() -- The shared black background already supplies zero.
        else
            if compact then
                cells[i]:SetColorTexture(math.floor(value / 64) % 8 / 7,
                    math.floor(value / 8) % 8 / 7, value % 8 / 7, 1)
            else
                cells[i]:SetColorTexture(math.floor(value / 4) % 2,
                    math.floor(value / 2) % 2, value % 2, 1)
            end
            cells[i]:Show()
        end
    end
end
-- MCT1: two-column binary locator, vertical calibration, serpentine data columns.
-- Build cell addresses once; no per-frame geometry or texture allocation.
local matrixSlots = {}
for x = 2, 38 do
    for row = 0, 9 do
        local y = x % 2 == 0 and row or 9 - row
        matrixSlots[#matrixSlots + 1] = y * 40 + x + 1
    end
end
matrixSlots[#matrixSlots + 1] = 40
matrixSlots[#matrixSlots + 1] = 400
local locator = {3, 2, 2, 3, 1, 0, 3, 0, 2, 1}
PaintHeader = function()
    if compact then
        for y = 0, 9 do
            Pixel(y * 40 + 1, math.floor(locator[y + 1] / 2) * 511)
            Pixel(y * 40 + 2, (locator[y + 1] % 2) * 511)
        end
        for level = 0, 7 do Pixel((level + 1) * 40 + 40, level * 73) end
        for i = 361, #matrixSlots do Pixel(matrixSlots[i], 0) end
    else
        for i = 1, COLS do Pixel(i, tonumber(SYNC:sub(i, i)) * 7) end
    end
end
PaintHeader()

local queue, qhead, qtail, recent, repeatIndex = {}, 1, 0, {}, 0
local cache, cacheBytes, manual, batchCounter = {}, 0, nil, 0
local layoutExport
local blockSyncQueued = false
local skipped, dropped, filtered, draws, unsent = 0, 0, 0, 0, 0
local OnUpdate
local elapsed, idleElapsed = 0, 0
local function Say(text) DEFAULT_CHAT_FRAME:AddMessage("[BianLingo] " .. text) end
local function QueueSize() return qtail - qhead + 1 end
local function TrimCache()
    local limit = Config().cacheLimit
    while #cache > limit or cacheBytes > 512 * 1024 do
        cacheBytes = cacheBytes - #table.remove(cache, 1).body
    end
    if UpdatePanel then UpdatePanel() end
end
local function Xor(a, b)
    local out, bit = 0, 1
    for _ = 1, 8 do
        if a % 2 ~= b % 2 then out = out + bit end
        a, b, bit = math.floor(a / 2), math.floor(b / 2), bit * 2
    end
    return out
end
local function CRC8(text)
    local crc = 0
    for i = 1, #text do
        crc = Xor(crc, text:byte(i))
        for _ = 1, 8 do
            local high = crc >= 128
            crc = (crc * 2) % 256
            if high then crc = Xor(crc, 7) end
        end
    end
    return crc
end
local function U16(n)
    return string.char(math.floor(n / 256) % 256, n % 256)
end
local function U32(n)
    return U16(math.floor(n / 65536)) .. U16(n % 65536)
end
local function Adler(s)
    local a, b = 1, 0
    for i = 1, #s do a = (a + s:byte(i)) % 65521; b = (b + a) % 65521 end
    return b * 65536 + a
end
-- Six bytes / 48 bits: signature, version/state/cell, corner2/seq14, CRC8.
UpdateMarker = function()
    if not enabled or (not Config().idleHide and not compact and chatState == 0) then marker:Hide(); return end
    local corner = ({tl = 0, tr = 1, bl = 2, br = 3})[Config().corner or "tl"] or 0
    local readableFrame = transmitting or (compact and not Config().idleHide)
    local version = compact and 64 or (chatState == 0 and 16 or 32)
    local hint = version == 16 and seq % 16384 or (chatState * 4096 + seq % 4096)
    local raw = string.char(252, 177, version + (readableFrame and 4 or 0) + (compact and 8 or 0) + CELL - (compact and 1 or 2))
        .. U16(corner * 16384 + hint)
    raw = raw .. string.char(CRC8(raw))
    if raw ~= lastMarker then
        lastMarker = raw
        local pixel = 1
        for i = 1, 6, 3 do
            local triple = raw:byte(i) * 65536 + raw:byte(i + 1) * 256 + raw:byte(i + 2)
            for shift = 21, 0, -3 do
                local value = math.floor(triple / 2 ^ shift) % 8
                markerCells[pixel]:SetColorTexture(math.floor(value / 4) % 2,
                    math.floor(value / 2) % 2, value % 2, 1)
                pixel = pixel + 1
            end
        end
    end
    marker:Show()
end
local function Appearance()
    if enabled and (transmitting or not Config().idleHide) then frame:Show() else frame:Hide() end
    UpdateMarker()
end
local function Wake()
    transmitting = true; idleElapsed = 0; repeatIndex = 0
    Appearance(); frame:SetScript("OnUpdate", OnUpdate)
end
local function PacketFor(message, part, batch)
    local size = batch and CAPACITY - 2 or CAPACITY
    local count = math.max(1, math.ceil(#message.body / size))
    local payload = message.body:sub((part - 1) * size + 1, part * size)
    if batch then payload = U16(batch) .. payload end
    local head = "BLG2" .. U32(session) .. U32(message.seq) .. U32(message.sent)
        .. U16(part) .. U16(count) .. U16(#payload)
        .. string.char(message.event + (batch and 128 or 0), message.player)
    local packet = head .. U32(Adler(head .. payload)) .. payload
    return packet .. string.rep("\0", 396 - #packet)
end
local function Control(event, batch, total)
    seq = seq + 1
    return {seq = seq, sent = GetServerTime(), event = event, player = 0,
        body = "\0" .. batch .. "\t" .. total}
end
-- Official Classic expansion clients may omit the retail secret-value API.
-- Forever and Retail still fail closed when that API is absent.
local classicChatRules = false
if type(GetBuildInfo) == "function" then
    local _, _, _, interface = GetBuildInfo()
    classicChatRules = type(interface) == "number" and (
        (interface >= 11500 and interface < 11600) or
        (interface >= 20500 and interface < 20600) or
        (interface >= 30400 and interface < 30500) or
        (interface >= 40400 and interface < 40500) or
        (interface >= 50500 and interface < 50600))
end
local function Secret(value)
    if type(issecretvalue) == "function" then return issecretvalue(value) end
    return not classicChatRules
end
local function SetChatState(value)
    if chatState ~= value then chatState = value; UpdateMarker() end
end
local function RefreshChatState()
    if type(C_ChatInfo) == "table" and type(C_ChatInfo.InChatMessagingLockdown) == "function" then
        local ok, value = pcall(C_ChatInfo.InChatMessagingLockdown)
        if ok and not Secret(value) and type(value) == "boolean" then
            SetChatState(value and 2 or 1); return true
        end
    end
    return false
end
local function Readable(s)
    if Secret(s) then return false end
    return type(s) == "string"
end
local function CleanText(text)
    -- Link labels are not the speaker's language. Preserve them in the packet,
    -- but omit them from the decision about whether the prose needs translation.
    text = text:gsub("|H.-|h.-|h", " "):gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", "")
    text = text:gsub("|cn[%w_]+:", "")
    text = text:gsub("|T.-|t", ""):gsub("|A.-|a", "")
    return text:match("^%s*(.-)%s*$")
end
local chineseTerms = {}
local traditionalMarkers = BianLingoTraditional or {}
for word in ("dps t n mt ot tank healer heal buff debuff aoe dot hot cd gcd oom afk brb gg ok boss lfg lfm pve pvp dk dh ss zs fs ms qs lr sm dz xd dkp roll"):gmatch("%S+") do
    chineseTerms[word] = true
end
local function IsChinese(text)
    -- Match the desktop's current Chinese rule, after removing WoW markup.
    if text:find("[\194-\223][\128-\191]") then return false end -- Preserve accented/Cyrillic prose.
    local han, foreign, traditional = false, false, false
    for a, b, c in text:gmatch("([\224-\239])([\128-\191])([\128-\191])") do
        local cp = (a:byte() - 224) * 4096 + (b:byte() - 128) * 64 + c:byte() - 128
        if cp >= 0x3400 and cp <= 0x9fff then han = true end
        if traditionalMarkers[cp] then traditional = true end
        if (cp >= 0x3040 and cp <= 0x30ff) or (cp >= 0xac00 and cp <= 0xd7af)
            or (cp >= 0x1100 and cp <= 0x11ff) or (cp >= 0x3130 and cp <= 0x318f) then foreign = true end
    end
    if not han or foreign then return false end
    if traditional then return true end -- Even when a Traditional message contains Latin names or prose.
    local letters, longWords, words = 0, 0, 0
    for word in text:gmatch("[A-Za-z]+") do
        if not chineseTerms[word:lower()] then
            words = words + 1
            letters = letters + #word
            if #word >= 3 then longWords = longWords + 1 end
        end
    end
    if letters >= 8 and (longWords >= 2 or (longWords >= 1 and words >= 4)) then
        return false
    end
    return true
end
local function IsNoise(text)
    local letters, consumed = 0, 0
    for char in text:gmatch("[%z\1-\127\194-\244][\128-\191]*") do
        consumed = consumed + #char
        local cp = char:byte()
        if #char > 1 then
            cp = cp % (2 ^ (7 - #char))
            for i = 2, #char do cp = cp * 64 + char:byte(i) - 128 end
        end
        if (cp >= 65 and cp <= 90) or (cp >= 97 and cp <= 122) then
            letters = letters + 1
            if letters > 1 then return false end
        elseif not ((cp >= 9 and cp <= 13) or (cp >= 32 and cp <= 64)
            or (cp >= 91 and cp <= 96) or (cp >= 123 and cp <= 126)
            or cp == 0xa0 or cp == 0xa1 or (cp >= 0xa2 and cp <= 0xa9)
            or cp == 0xab or cp == 0xac or cp == 0xae or cp == 0xb0
            or cp == 0xb1 or cp == 0xb7 or cp == 0xbb or cp == 0xbf
            or cp == 0xd7 or cp == 0xf7
            or (cp >= 0x2000 and cp <= 0x206f) or (cp >= 0x20a0 and cp <= 0x20cf)
            or cp == 0x20e3 or (cp >= 0x2190 and cp <= 0x2bff)
            or (cp >= 0x3000 and cp <= 0x3003) or (cp >= 0x3008 and cp <= 0x301f)
            or (cp >= 0xfe00 and cp <= 0xfe0f) or (cp >= 0xfe10 and cp <= 0xfe19)
            or (cp >= 0xfe30 and cp <= 0xfe52) or (cp >= 0xfe54 and cp <= 0xfe66)
            or (cp >= 0xfe68 and cp <= 0xfe6b)
            or (cp >= 0xff01 and cp <= 0xff20) or (cp >= 0xff3b and cp <= 0xff40)
            or (cp >= 0xff5b and cp <= 0xff65) or (cp >= 0xffe0 and cp <= 0xffee)
            or (cp >= 0x1f000 and cp <= 0x1faff)) then
            return false -- Preserve foreign letters and unknown Unicode characters.
        end
    end
    return consumed == #text
end
local events = {
    "CHAT_MSG_SAY", "CHAT_MSG_YELL", "CHAT_MSG_WHISPER", "CHAT_MSG_WHISPER_INFORM",
    "CHAT_MSG_PARTY", "CHAT_MSG_PARTY_LEADER", "CHAT_MSG_RAID", "CHAT_MSG_RAID_LEADER",
    "CHAT_MSG_RAID_WARNING", "CHAT_MSG_INSTANCE_CHAT", "CHAT_MSG_INSTANCE_CHAT_LEADER",
    "CHAT_MSG_GUILD", "CHAT_MSG_OFFICER", "CHAT_MSG_CHANNEL",
    "CHAT_MSG_BN_WHISPER", "CHAT_MSG_BN_WHISPER_INFORM", "CHAT_MSG_COMMUNITIES_CHANNEL",
}
-- Stable wire IDs: append new entries, never reorder.
local eventIDs, classIDs = {}, {}
local playerCache, playerOrder = {}, {}
for id, name in ipairs(events) do eventIDs[name] = id end
eventIDs.BRIDGE_READY = #events + 1
eventIDs.BRIDGE_TEST = #events + 2
eventIDs.BRIDGE_REPLAY_BEGIN = #events + 3
eventIDs.BRIDGE_REPLAY_END = #events + 4
eventIDs.BRIDGE_CHAT_LAYOUT = #events + 5
eventIDs.BRIDGE_BLOCKLIST = #events + 6
for id, name in ipairs({"WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "DEATHKNIGHT",
    "SHAMAN", "MAGE", "WARLOCK", "MONK", "DRUID", "DEMONHUNTER", "EVOKER"}) do classIDs[name] = id end
-- Cache our own realm once on login. No per-chat realm API or additional player lookup.
local homeRealm = ""
local function RealmKey(value)
    if not Readable(value) then return "" end
    return (value:gsub("[%s%-]", ""):lower())
end
local function CacheHomeRealm()
    homeRealm = ""
    for _, api in ipairs({"GetNormalizedRealmName", "GetRealmName"}) do
        if type(_G[api]) == "function" then
            local ok, value = pcall(_G[api])
            if ok then homeRealm = RealmKey(value) end
            if homeRealm ~= "" then return end
        end
    end
end
local function Enqueue(event, text, author, guid)
    if not enabled then return end
    if not Readable(text) then
        if event:sub(1, 9) == "CHAT_MSG_" and Secret(text) then SetChatState(2) end
        skipped = skipped + 1; return
    end
    if event:sub(1, 9) == "CHAT_MSG_" and not RefreshChatState() and chatState == 2 then SetChatState(1) end
    local sent = GetServerTime()
    if event:sub(1, 9) == "CHAT_MSG_" then
        local clean = CleanText(text)
        if IsNoise(clean) then
            filtered = filtered + 1; return
        end
        if (not BianLingoDB or BianLingoDB.skipChinese ~= false) and IsChinese(clean) then
            filtered = filtered + 1; return
        end
    end
    if not Readable(author) then author = "" end
    if event:sub(1, 9) == "CHAT_MSG_" and event:sub(1, 12) ~= "CHAT_MSG_BN_" and
        not event:find("CHAT_MSG_COMMUNITIES_CHANNEL", 1, true) and BLGBlocklist.IsBlocked(Config(), author, homeRealm) then
        filtered = filtered + 1; return
    end
    local classID, details, knownRealm = 0, nil, ""
    -- Resolve only accepted messages; never inspect or export secret GUIDs/classes.
    if Readable(guid) and guid ~= "" and type(GetPlayerInfoByGUID) == "function" then
        local cached = playerCache[guid]
        if cached then
            classID, details, knownRealm = cached[1], cached[2], cached[3] or ""
        else
            local ok, localizedClass, class, localizedRace, englishRace, sex, playerName, realmName =
                pcall(GetPlayerInfoByGUID, guid)
            if ok then
                if Readable(class) then classID = classIDs[class] or 0 end
                local function Safe(value)
                    if not Readable(value) then return "" end
                    if #value > 96 then return "" end -- Never truncate a UTF-8 codepoint.
                    return (value:gsub("[%z\t\r\n]", " ")) -- gsub also returns a count; never append it to the packet.
                end
                local fields = {Safe(localizedClass), Safe(localizedRace), Safe(englishRace),
                    Safe(playerName), Safe(realmName)}
                knownRealm = RealmKey(fields[5])
                local safeSex = 0
                if not Secret(sex) and
                    type(sex) == "number" and sex >= 1 and sex <= 3 then
                    safeSex = sex
                end
                if safeSex ~= 0 or table.concat(fields) ~= "" then
                    details = table.concat(fields, "\0") .. "\0" .. string.char(safeSex)
                end
                if classID ~= 0 or details then
                    playerCache[guid] = {classID, details, knownRealm}
                    playerOrder[#playerOrder + 1] = guid
                    if #playerOrder > 256 then
                        playerCache[table.remove(playerOrder, 1)] = nil
                    end
                end
            end
        end
    end
    if event:sub(1, 9) == "CHAT_MSG_" and event:sub(1, 12) ~= "CHAT_MSG_BN_" and
        event ~= "CHAT_MSG_COMMUNITIES_CHANNEL" and BLGBlocklist.IsBlocked(Config(), author, knownRealm) then
        filtered = filtered + 1; return
    end
    local kind, channelData = event:match("^([^\t]+)\t(.*)$")
    kind = kind or event
    local eventID = eventIDs[kind]
    if not eventID then return end
    -- Read the same color table as native chat. No player or item lookup.
    local rgb = ""
    local colorKey = kind:sub(10)
    if kind == "CHAT_MSG_CHANNEL" and channelData then
        local index = channelData:match("^(%d+)\t")
        if index then colorKey = "CHANNEL" .. index end
    end
    local info = type(ChatTypeInfo) == "table" and (ChatTypeInfo[colorKey] or ChatTypeInfo[kind:sub(10)])
    if type(info) == "table" then
        local parts = {}
        for _, key in ipairs({"r", "g", "b"}) do
            local value = info[key]
            if Secret(value) or type(value) ~= "number" or value ~= value then parts = {}; break end
            parts[#parts + 1] = math.floor(math.max(0, math.min(1, value)) * 255 + 0.5)
        end
        if #parts == 3 then rgb = string.char(unpack(parts)) end
    end
    local body = (kind == "CHAT_MSG_CHANNEL" and ((channelData or "\t") .. "\0") or "")
        .. (details or "") .. author .. "\0" .. text
    body = rgb .. body
    if #body > 16384 then dropped = dropped + 1; return end
    seq = seq + 1
    local authorRealm = RealmKey(author:match("^[^-]+%-(.+)$"))
    local targetRealm = authorRealm ~= "" and authorRealm or knownRealm
    local sameRealm = kind:sub(1, 9) == "CHAT_MSG_" and homeRealm ~= "" and targetRealm == homeRealm
        and (knownRealm == "" or knownRealm == homeRealm)
    local message = {seq = seq, sent = sent, event = eventID,
        player = classID + (sameRealm and 32 or 0) + (details and 128 or 0) + (#rgb == 3 and 64 or 0), body = body,
        identity = author, identityRealm = targetRealm ~= "" and targetRealm or homeRealm}
    if kind:sub(1, 9) == "CHAT_MSG_" then
        cache[#cache + 1] = message; cacheBytes = cacheBytes + #body; TrimCache()
    end
    local count = math.max(1, math.ceil(#body / CAPACITY))
    if QueueSize() + count > 256 then dropped = dropped + 1; unsent = unsent + 1; return end
    for part = 1, count do
        qtail = qtail + 1; queue[qtail] = PacketFor(message, part)
    end
    Wake()
end
-- Explicit snapshot only. Does not query players, hook chat drawing, or poll settings.
local function ExportChatWindows()
    if not enabled then Say(BLGText("Run /bl on first")); return end
    if manual or layoutExport then Say(BLGText("Replay or synchronization is already active; wait for it to finish.")); return end
    local function Short(value, limit)
        if not Readable(value) then error(BLGText("The game chat layout contains an unreadable value")) end
        local out, n = {}, 0
        for char in value:gmatch("[%z\1-\127\194-\244][\128-\191]*") do
            n = n + 1; if n > limit then break end; out[#out + 1] = char
        end
        return table.concat(out)
    end
    local function Quote(value)
        local slash = string.char(92)
        -- JSON escapes ASCII controls only. %c is locale-dependent and can
        -- classify UTF-8 continuation bytes as controls on macOS.
        return '"' .. value:gsub('[%z\1-\31' .. slash .. '"]', function(c)
            if c == '"' then return slash .. '"' end
            if c == slash then return slash .. slash end
            return slash .. string.format('u%04x', c:byte())
        end) .. '"'
    end
    local function Array(values)
        local out = {}; for _, value in ipairs(values) do out[#out + 1] = Quote(value) end
        return '[' .. table.concat(out, ',') .. ']'
    end
    local ok, snapshot = pcall(function()
        if type(GetChatWindowInfo) ~= "function" or type(GetChatWindowMessages) ~= "function"
            or type(GetChatWindowChannels) ~= "function" then error(BLGText("This client has no chat-window export API")) end
        local windows = {}
        for index = 1, math.min(tonumber(NUM_CHAT_WINDOWS) or 10, 16) do
            local name, _, _, _, _, _, shown = GetChatWindowInfo(index)
            name = Short(name or "", 24)
            if index == 1 or name ~= "" or (not Secret(shown) and shown) then
                local kinds, selected = {}, {}
                for _, group in ipairs({GetChatWindowMessages(index)}) do
                    if not Readable(group) then error(BLGText("Unreadable message type")) end
                    local values = type(ChatTypeGroup) == "table" and ChatTypeGroup[group]
                    if type(values) == "table" then
                        for _, event in pairs(values) do
                            if Readable(event) and eventIDs[event] and eventIDs[event] <= 17 then selected[event] = true end
                        end
                    elseif eventIDs["CHAT_MSG_" .. group] then selected["CHAT_MSG_" .. group] = true end
                end
                local raw, channels = {GetChatWindowChannels(index)}, {}
                for i = 1, #raw, 2 do
                    if #channels >= 64 then error(BLGText("Too many channels")) end
                    channels[#channels + 1] = Short(raw[i], 128)
                end
                selected.CHAT_MSG_CHANNEL = #channels > 0 and true or nil
                for i = 1, 17 do if selected[events[i]] then kinds[#kinds + 1] = events[i] end end
                if #kinds > 0 then
                    if name == "" then name = index == 1 and BLGText("General") or (BLGText("Window") .. index) end
                    windows[#windows + 1] = '{"id":"wow-' .. index .. '","name":' .. Quote(name) ..
                        ',"events":' .. Array(kinds) .. ',"channels":' .. Array(channels) .. ',"exclude_channels":[]}'
                end
            end
        end
        if #windows == 0 then error(BLGText("No chat window can be exported; combat logs are not part of the chat bridge")) end
        local json = '{"schemaVersion":1,"windows":[' .. table.concat(windows, ',') .. ']}'
        if #json > 12288 then error(BLGText("The window layout exceeds 12 KiB")) end
        return json
    end)
    if not ok then Say(BLGText("Could not prepare synchronization: ") .. tostring(snapshot)); return end
    seq = seq + 1
    local message = {seq = seq, sent = GetServerTime(), event = eventIDs.BRIDGE_CHAT_LAYOUT, player = 0, body = "\0" .. snapshot}
    layoutExport = {message = message, part = 1, count = math.ceil(#message.body / CAPACITY)}
    Wake(); Say(BLGText("Sending the chat layout; keep the game visible. The desktop app will replace and save all tabs when complete."))
end
local function ExportBlocklist()
    if not enabled then Say(BLGText("Run /bl on first")); return end
    if manual or layoutExport then
        blockSyncQueued = true; Say(BLGText("Queued; the latest blocklist will be synchronized after the current transmission.")); return
    end
    local snapshot = BLGBlocklist.Snapshot(Config())
    local chunks, entries = {}, {}
    local function Quote(value)
        return '"' .. value:gsub('\\', '\\\\'):gsub('"', '\\"') .. '"'
    end
    local function Finish()
        local body = '\0{"schemaVersion":1,"entries":[' .. table.concat(entries, ',') .. ']}'
        seq = seq + 1
        local message = {seq = seq, sent = GetServerTime(), event = eventIDs.BRIDGE_BLOCKLIST, player = 0, body = body}
        chunks[#chunks + 1] = {message = message, part = 1, count = math.ceil(#body / CAPACITY)}
        entries = {}
    end
    for _, r in ipairs(snapshot.entries) do
        entries[#entries + 1] = '[' .. Quote(r[1]) .. ',' .. string.format("%.0f", r[2]) .. ',' .. Quote(r[3]) .. ',' .. tostring(r[4]) .. ']'
        if #entries == 32 then Finish() end
    end
    if #entries > 0 or #chunks == 0 then Finish() end
    for i = 1, #chunks - 1 do chunks[i].next = chunks[i + 1] end
    layoutExport = chunks[1]
    recent = {}; repeatIndex = 0
    Wake()
    Say(BLGText("Sending the blocklist to the desktop app. Import desktop edits by pasting a sync code in game."))
end
local function StartReplay(number)
    if not enabled then Say(BLGText("Run /bl on first")); return end
    if manual or layoutExport then Say(BLGText("Replay or synchronization is already active; wait for it to finish.")); return end
    local n = number == "" and 20 or tonumber(number)
    if not n or n ~= math.floor(n) or n < 1 or n > 100 then Say(BLGText("Replay count must be between 1 and 100.")); return end
    local items = {}
    for i = math.max(1, #cache - n + 1), #cache do
        local message = cache[i]
        if not BLGBlocklist.IsBlocked(Config(), message.identity, message.identityRealm) then items[#items + 1] = message end
    end
    if #items == 0 then Say(BLGText("No cached chat is available for replay.")); return end
    batchCounter = batchCounter % 65535 + 1
    manual = {id = batchCounter, items = items, index = 1, part = 1, phase = "begin"}
    recent = {}; repeatIndex = 0
    Say(BLGText("Replaying the latest ") .. #items .. BLGText(" messages; live chat has priority."))
    if UpdatePanel then UpdatePanel() end
    Wake()
end
local matrixDivisors = {2097152, 262144, 32768, 4096, 512, 64, 8, 1}
Draw = function(packet)
    if packet == lastPacket then return end
    lastPacket = packet
    draws = draws + 1
    if compact then
        -- Optical envelope is separate from the unchanged logical BLG2 packet.
        -- Its checksum covers all 396 bytes including padding. One zero byte
        -- makes three equal 1080-bit channel planes; each cell holds 3 bits/plane.
        local wire = "MCT1" .. U32(Adler(packet)) .. packet .. "\0"
        local cell, floor = 1, math.floor
        -- Read the three planes in parallel, without an intermediate symbol
        -- array or per-frame powers/geometry. Each iteration paints eight cells.
        for i = 1, 135, 3 do
            local r = wire:byte(i) * 65536 + wire:byte(i + 1) * 256 + wire:byte(i + 2)
            local g = wire:byte(i + 135) * 65536 + wire:byte(i + 136) * 256 + wire:byte(i + 137)
            local b = wire:byte(i + 270) * 65536 + wire:byte(i + 271) * 256 + wire:byte(i + 272)
            for j = 1, 8 do
                local divisor = matrixDivisors[j]
                Pixel(matrixSlots[cell], floor(r / divisor) % 8 * 64 + floor(g / divisor) % 8 * 8 + floor(b / divisor) % 8)
                cell = cell + 1
            end
        end
        return
    end
    local pixel = COLS + 1
    for i = 1, #packet, 3 do
        local triple = packet:byte(i) * 65536 + packet:byte(i + 1) * 256 + packet:byte(i + 2)
        for shift = 21, 0, -3 do
            Pixel(pixel, math.floor(triple / 2 ^ shift) % 8)
            pixel = pixel + 1
        end
    end
end
local function Remember(packet)
    recent[#recent + 1] = packet
    if #recent > 32 then table.remove(recent, 1) end
end
OnUpdate = function(_, dt)
    elapsed = elapsed + dt
    if elapsed < 0.2 then return end
    local tick = elapsed; elapsed = 0
    if blockSyncQueued and not manual and not layoutExport and QueueSize() == 0 then
        blockSyncQueued = false; ExportBlocklist()
    end
    local packet
    if QueueSize() > 0 then
        packet = queue[qhead]; queue[qhead] = nil; qhead = qhead + 1
        if qhead > qtail then queue = {}; qhead = 1; qtail = 0 end
    elseif manual and manual.phase == "begin" then
        packet = PacketFor(Control(eventIDs.BRIDGE_REPLAY_BEGIN, manual.id, #manual.items), 1)
        manual.phase = "items"
    elseif manual and manual.phase == "items" then
        local message = manual.items[manual.index]
        if message then
            packet = PacketFor(message, manual.part, manual.id)
            manual.part = manual.part + 1
            if manual.part > math.max(1, math.ceil(#message.body / (CAPACITY - 2))) then
                manual.index = manual.index + 1; manual.part = 1
            end
        else manual.phase = "tail" end
    elseif layoutExport then
        packet = PacketFor(layoutExport.message, layoutExport.part)
        layoutExport.part = layoutExport.part + 1
        if layoutExport.part > layoutExport.count then layoutExport = layoutExport.next end
    end
    if packet then
        Remember(packet); Draw(packet); idleElapsed = 0; return
    end
    if repeatIndex < #recent then
        repeatIndex = repeatIndex + 1; Draw(recent[repeatIndex]); idleElapsed = 0; return
    end
    if manual then
        if manual.phase == "tail" then
            packet = PacketFor(Control(eventIDs.BRIDGE_REPLAY_END, manual.id, #manual.items), 1)
            manual.phase = "finish"; manual.last = packet
            Draw(packet); idleElapsed = 0; return
        elseif manual.phase == "finish" then
            Draw(manual.last); manual.phase = "done"; return
        else
            Say(BLGText("Replay transmission completed: ") .. #manual.items .. BLGText(" messages. Check Status in the desktop app for reception results."))
            manual = nil
            if UpdatePanel then UpdatePanel() end
        end
    end
    recent = {}; repeatIndex = 0
    idleElapsed = idleElapsed + tick
    if not Config().idleHide or idleElapsed >= 0.8 then
        transmitting = false; Appearance()
        frame:SetScript("OnUpdate", nil)
    end
end

local panel, optionsPanel, replayButton
local ShowSettings
local ShowBlocklist
local blockPanel
local function PositionReplayButton()
    if not replayButton then return end
    replayButton:ClearAllPoints()
    if Minimap then
        local angle = math.rad(Config().minimapAngle)
        -- An attachment to Minimap follows its movement, scale and UI visibility.
        replayButton:SetPoint("CENTER", Minimap, "CENTER",
            math.cos(angle) * (Minimap:GetWidth() / 2 + 8),
            math.sin(angle) * (Minimap:GetHeight() / 2 + 8))
    else
        replayButton:SetPoint("TOPRIGHT", UIParent, "TOPRIGHT", -30, -180)
    end
end
local function DragReplayButton()
    if not Minimap then return end
    local mx, my = Minimap:GetCenter()
    if not mx or not my then return end
    local x, y = GetCursorPosition()
    local scale = Minimap:GetEffectiveScale()
    Config().minimapAngle = math.deg(math.atan2(y / scale - my, x / scale - mx)) % 360
    PositionReplayButton()
end
local function UpdateReplayButton()
    if not replayButton then
        replayButton = CreateFrame("Button", "BianLingoBridgeReplayButton", Minimap or UIParent)
        replayButton:SetSize(31, 31); replayButton:SetFrameStrata("MEDIUM")
        replayButton:SetFrameLevel(Minimap and Minimap:GetFrameLevel() + 8 or 8)
        replayButton:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        replayButton:RegisterForDrag("LeftButton")
        local backing = replayButton:CreateTexture(nil, "BACKGROUND")
        backing:SetTexture("Interface/Minimap/UI-Minimap-Background")
        backing:SetSize(24, 24); backing:SetPoint("CENTER")
        local icon = replayButton:CreateTexture(nil, "ARTWORK")
        icon:SetTexture("Interface/AddOns/BianLingo/Textures/Logo")
        icon:SetTexCoord(0, 1, 0, 1); icon:SetSize(20, 20); icon:SetPoint("CENTER")
        local border = replayButton:CreateTexture(nil, "OVERLAY")
        border:SetTexture("Interface/Minimap/MiniMap-TrackingBorder")
        border:SetSize(53, 53); border:SetPoint("TOPLEFT")
        replayButton:SetHighlightTexture("Interface/Minimap/UI-Minimap-ZoomButton-Highlight", "ADD")
        replayButton:SetScript("OnClick", function(_, mouse)
            if mouse == "RightButton" then StartReplay("") else ShowSettings() end
        end)
        replayButton:SetScript("OnDragStart", function(self)
            GameTooltip:Hide()
            self:SetScript("OnUpdate", DragReplayButton)
            DragReplayButton()
        end)
        replayButton:SetScript("OnDragStop", function(self)
            self:SetScript("OnUpdate", nil); DragReplayButton()
        end)
        replayButton:SetScript("OnHide", function(self) self:SetScript("OnUpdate", nil) end)
        replayButton:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_LEFT"); GameTooltip:SetText("BianLingo")
            GameTooltip:AddLine(BLGText("Left-click: settings\nRight-click: replay recent messages\nDrag: move around the minimap"), 1, 1, 1)
            GameTooltip:Show()
        end)
        replayButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
    end
    PositionReplayButton()
    replayButton:SetShown(Config().showReplayButton == true)
end
local settingsViews = {}
UpdatePanel = function()
    for _, view in ipairs(settingsViews) do
        if view.frame:IsShown() then
            view.enabled:SetChecked(Config().enabled)
            view.hide:SetChecked(not not Config().idleHide)
            view.native:SetChecked(Config().mode == "native")
            view.compact:SetChecked(Config().mode == "compact")
            view.replayButton:SetChecked(Config().showReplayButton == true)
            if not view.input:HasFocus() then view.input:SetText(tostring(Config().cacheLimit)) end
            view.count:SetText(BLGText("Cached messages: ") .. #cache .. BLGText(" / ") .. Config().cacheLimit .. BLGText(" messages") ..
                (manual and BLGText(" · Replaying") or "") .. BLGText("\nNot queued so far: ") .. unsent .. BLGText(" messages (available for manual replay)"))
        end
    end
end
local function SetLimit(value)
    local n = tonumber(value)
    if not n or n ~= math.floor(n) or n < 1 or n > 100 then Say(BLGText("The cache limit must be between 1 and 100.")); return false end
    Config().cacheLimit = n; TrimCache(); return true
end
local function SetMode(mode)
    local db = Config(); db.mode = mode
    if mode == "compact" then db.cell = 1 elseif db.cell == 1 then db.cell = 2 end
    Layout(); UpdatePanel()
end
local lifecycleEvents = {"UI_SCALE_CHANGED", "DISPLAY_SIZE_CHANGED", "ADDON_RESTRICTION_STATE_CHANGED",
    "ENCOUNTER_END", "CHALLENGE_MODE_COMPLETED", "PVP_MATCH_COMPLETE", "PLAYER_ENTERING_WORLD"}
local function SetEnabled(value, force)
    local db = Config()
    local changed = enabled ~= value
    db.enabled, enabled = value, value
    for _, name in ipairs(events) do
        if value then pcall(frame.RegisterEvent, frame, name) else pcall(frame.UnregisterEvent, frame, name) end
    end
    for _, name in ipairs(lifecycleEvents) do
        if value then pcall(frame.RegisterEvent, frame, name) else pcall(frame.UnregisterEvent, frame, name) end
    end
    if value then
        CacheHomeRealm(); RefreshChatState(); Appearance()
        if changed or force then Enqueue("BRIDGE_READY", BLGText("Screen bridge ready"), "") end
    else
        transmitting = false
        queue = {}; qhead = 1; qtail = 0; recent = {}; manual = nil; layoutExport = nil; blockSyncQueued = false
        cache = {}; cacheBytes = 0; playerCache = {}; playerOrder = {}; lastPacket = nil
        repeatIndex = 0; elapsed = 0; idleElapsed = 0
        frame:SetScript("OnUpdate", nil); frame:Hide(); marker:Hide()
        if replayButton then replayButton:SetScript("OnUpdate", nil) end
    end
    UpdatePanel()
end
local function BuildSettings(parent, prefix)
    local view = {frame = parent}
    settingsViews[#settingsViews + 1] = view
    local title = parent:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    title:SetPoint("TOPLEFT", 18, -18); title:SetText(BLGText("BianLingo · Chat bridge"))
    view.enabled = CreateFrame("CheckButton", prefix .. "Enabled", parent, "UICheckButtonTemplate")
    view.enabled:SetPoint("TOPLEFT", 250, -14)
    local enabledLabel = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    enabledLabel:SetPoint("LEFT", view.enabled, "RIGHT", 3, 0); enabledLabel:SetText(BLGText("Enable addon"))
    view.enabled:SetScript("OnClick", function(self) SetEnabled(not not self:GetChecked()) end)
    view.hide = CreateFrame("CheckButton", prefix .. "IdleHide", parent, "UICheckButtonTemplate")
    view.hide:SetPoint("TOPLEFT", 15, -48)
    local hideLabel = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    hideLabel:SetPoint("LEFT", view.hide, "RIGHT", 3, 0); hideLabel:SetText(BLGText("Hide when idle (keep the status marker)"))
    view.hide:SetScript("OnClick", function(self)
        Config().idleHide = not not self:GetChecked(); Appearance(); UpdatePanel()
    end)
    local label = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    label:SetPoint("TOPLEFT", 20, -99); label:SetText(BLGText("Cache limit (1–100):"))
    view.input = CreateFrame("EditBox", prefix .. "CacheLimit", parent, "InputBoxTemplate")
    view.input:SetSize(45, 25); view.input:SetPoint("TOPLEFT", 190, -92); view.input:SetAutoFocus(false)
    view.input:SetNumeric(true); view.input:SetMaxLetters(3)
    local function ApplyLimit() if SetLimit(view.input:GetText()) then view.input:ClearFocus() end end
    view.input:SetScript("OnEnterPressed", ApplyLimit)
    local apply = CreateFrame("Button", prefix .. "Save", parent, "UIPanelButtonTemplate")
    apply:SetSize(70, 25); apply:SetPoint("TOPLEFT", 250, -92); apply:SetText(BLGText("Save"))
    apply:SetScript("OnClick", ApplyLimit)
    view.count = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    view.count:SetPoint("TOPLEFT", 20, -134)
    local replay = CreateFrame("Button", prefix .. "Replay", parent, "UIPanelButtonTemplate")
    replay:SetSize(175, 28); replay:SetPoint("TOPLEFT", 20, -177); replay:SetText(BLGText("Replay recent messages"))
    replay:SetScript("OnClick", function() StartReplay("") end)
    local hint = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    hint:SetPoint("LEFT", replay, "RIGHT", 10, 0); hint:SetText(BLGText("At most 20 by default"))
    local modeLabel = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    modeLabel:SetPoint("TOPLEFT", 20, -228); modeLabel:SetText(BLGText("Pixel mode:"))
    view.native = CreateFrame("CheckButton", prefix .. "NativeMode", parent, "UIRadioButtonTemplate")
    view.native:SetPoint("TOPLEFT", 125, -220)
    local nativeLabel = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    nativeLabel:SetPoint("LEFT", view.native, "RIGHT", 3, 0); nativeLabel:SetText(BLGText("Black & white (HDR compatible)"))
    view.native:SetScript("OnClick", function() SetMode("native") end)
    view.compact = CreateFrame("CheckButton", prefix .. "CompactMode", parent, "UIRadioButtonTemplate")
    view.compact:SetPoint("TOPLEFT", 125, -247)
    local compactLabel = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    compactLabel:SetPoint("LEFT", view.compact, "RIGHT", 3, 0); compactLabel:SetText(BLGText("Compact matrix"))
    view.compact:SetScript("OnClick", function() SetMode("compact") end)
    local modeHint = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    modeHint:SetPoint("TOPLEFT", 20, -276); modeHint:SetWidth(355); modeHint:SetJustifyH("LEFT")
    modeHint:SetText(BLGText("Compact: 42 x 12 pixels by default (SDR). For HDR choose Black & white. Both modes need a matching desktop version."))
    view.replayButton = CreateFrame("CheckButton", prefix .. "ShowReplayButton", parent, "UICheckButtonTemplate")
    view.replayButton:SetPoint("TOPLEFT", 15, -339)
    local buttonLabel = parent:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    buttonLabel:SetPoint("LEFT", view.replayButton, "RIGHT", 3, 0); buttonLabel:SetText(BLGText("Show minimap button (drag around the edge)"))
    view.replayButton:SetScript("OnClick", function(self)
        Config().showReplayButton = not not self:GetChecked(); UpdateReplayButton(); UpdatePanel()
    end)
    local buttonHint = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    buttonHint:SetPoint("TOPLEFT", 20, -376); buttonHint:SetText(BLGText("Left-click for settings, right-click to replay. Drag around the minimap to save its position."))
    local export = CreateFrame("Button", prefix .. "ExportWindows", parent, "UIPanelButtonTemplate")
    export:SetSize(175, 28); export:SetPoint("TOPLEFT", 20, -405); export:SetText(BLGText("Synchronize chat windows"))
    export:SetScript("OnClick", ExportChatWindows)
    local exportHint = parent:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    exportHint:SetPoint("LEFT", export, "RIGHT", 10, 0); exportHint:SetText(BLGText("Replace and save all desktop tabs"))
    local blocks = CreateFrame("Button", prefix .. "Blocklist", parent, "UIPanelButtonTemplate")
    blocks:SetSize(175, 28); blocks:SetPoint("TOPLEFT", 20, -447); blocks:SetText(BLGText("Blocklist…"))
    blocks:SetScript("OnClick", function() ShowBlocklist() end)
    parent:SetScript("OnShow", UpdatePanel)
end
ShowSettings = function()
    if blockPanel then
        if blockPanel.importer then blockPanel.importer:Hide() end
        blockPanel:Hide()
    end
    if not panel then
        panel = CreateFrame("Frame", "BianLingoBridgeSettings", UIParent, "BackdropTemplate")
        panel:SetSize(440, 505); panel:SetPoint("CENTER"); panel:SetFrameStrata("DIALOG")
        panel:SetMovable(true); panel:EnableMouse(true); panel:RegisterForDrag("LeftButton")
        panel:SetScript("OnDragStart", panel.StartMoving); panel:SetScript("OnDragStop", panel.StopMovingOrSizing)
        panel:SetBackdrop({bgFile = "Interface/Buttons/WHITE8X8", edgeFile = "Interface/Buttons/WHITE8X8", edgeSize = 1})
        panel:SetBackdropColor(0.05, 0.07, 0.1, 0.96); panel:SetBackdropBorderColor(0.2, 0.6, 0.65, 1)
        local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", -2, -2)
        BuildSettings(panel, "BianLingoBridgeSettings")
        UISpecialFrames = UISpecialFrames or {}; UISpecialFrames[#UISpecialFrames + 1] = "BianLingoBridgeSettings"
    end
    panel:Show(); UpdatePanel()
end
ShowBlocklist = function()
    if panel then panel:Hide() end
    for _, name in ipairs({"SettingsPanel", "InterfaceOptionsFrame"}) do
        local host = _G[name]
        if host and host.IsShown and host:IsShown() then
            if type(HideUIPanel) == "function" then HideUIPanel(host) else host:Hide() end
        end
    end
    if not blockPanel then
        local p = CreateFrame("Frame", "BianLingoBridgeBlocklist", UIParent, "BackdropTemplate")
        blockPanel = p
        p:SetSize(560, 500); p:SetPoint("CENTER"); p:SetFrameStrata("FULLSCREEN_DIALOG"); p:SetFrameLevel(150)
        p:SetMovable(true); p:EnableMouse(true); p:RegisterForDrag("LeftButton")
        p:SetScript("OnDragStart", p.StartMoving); p:SetScript("OnDragStop", p.StopMovingOrSizing)
        p:SetBackdrop({bgFile = "Interface/Buttons/WHITE8X8", edgeFile = "Interface/Buttons/WHITE8X8", edgeSize = 1})
        p:SetBackdropColor(0.05, 0.07, 0.1, 1); p:SetBackdropBorderColor(0.2, 0.6, 0.65, 1)
        local close = CreateFrame("Button", nil, p, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", -2, -2)
        local title = p:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
        title:SetPoint("TOPLEFT", 20, -20); title:SetText(BLGText("Translation blocklist"))
        local hint = p:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        hint:SetPoint("TOPLEFT", 20, -52); hint:SetWidth(510); hint:SetJustifyH("LEFT")
        hint:SetText(BLGText("Filters this translator only, not Blizzard's ignore list. A name without a realm matches all characters with that name."))
        local scroll = CreateFrame("ScrollFrame", "BianLingoBridgeBlockScroll", p, "UIPanelScrollFrameTemplate")
        scroll:SetPoint("TOPLEFT", 20, -90); scroll:SetSize(490, 245)
        local content = CreateFrame("Frame", nil, scroll); content:SetSize(480, 245); scroll:SetScrollChild(content)
        p.rows, p.content = {}, content
        p.input = CreateFrame("EditBox", "BianLingoBridgeBlockName", p, "InputBoxTemplate")
        p.input:SetSize(315, 26); p.input:SetPoint("TOPLEFT", 25, -353); p.input:SetAutoFocus(false); p.input:SetMaxLetters(192)
        p.status = p:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        p.status:SetPoint("TOPLEFT", 20, -446); p.status:SetWidth(510); p.status:SetJustifyH("LEFT")
        local function Refresh()
            local names = BLGBlocklist.Names(Config())
            for i, name in ipairs(names) do
                if not p.rows[i] then
                    local row = CreateFrame("Button", nil, content, "UIPanelButtonTemplate")
                    row:SetSize(470, 23); row:SetPoint("TOPLEFT", 0, -(i - 1) * 27)
                    row:SetScript("OnClick", function(self) p.input:SetText(self.playerName) end)
                    p.rows[i] = row
                end
                p.rows[i].playerName = name; p.rows[i]:SetText(name); p.rows[i]:Show()
            end
            for i = #names + 1, #p.rows do p.rows[i]:Hide() end
            content:SetHeight(math.max(245, #names * 27))
            p.status:SetText(BLGBlocklist.error or (BLGText("Blocked players: ") .. #names .. BLGText(". Import desktop edits using a sync code. The game saves on /reload or normal exit.")))
        end
        local function Change(blocked)
            local ok, err = pcall(BLGBlocklist.Set, Config(), p.input:GetText(), blocked)
            if ok then p.input:ClearFocus(); Refresh(); ExportBlocklist() else p.status:SetText(tostring(err)) end
        end
        local add = CreateFrame("Button", "BianLingoBridgeBlockAdd", p, "UIPanelButtonTemplate")
        add:SetSize(160, 27); add:SetPoint("TOPLEFT", 365, -352); add:SetText(BLGText("Add block"))
        add:SetScript("OnClick", function() Change(true) end)
        p.input:SetScript("OnEnterPressed", function() Change(true) end)
        local remove = CreateFrame("Button", "BianLingoBridgeBlockRemove", p, "UIPanelButtonTemplate")
        remove:SetSize(150, 28); remove:SetPoint("TOPLEFT", 20, -400); remove:SetText(BLGText("Unblock entered name"))
        remove:SetScript("OnClick", function() Change(false) end)
        local sync = CreateFrame("Button", "BianLingoBridgeBlockSync", p, "UIPanelButtonTemplate")
        sync:SetSize(150, 28); sync:SetPoint("TOPLEFT", 190, -400); sync:SetText(BLGText("Sync desktop"))
        sync:SetScript("OnClick", ExportBlocklist)
        local importer = CreateFrame("Frame", "BianLingoBridgeBlockImport", UIParent, "BackdropTemplate")
        p.importer = importer
        importer:SetSize(580, 425); importer:SetPoint("CENTER"); importer:SetFrameStrata("FULLSCREEN_DIALOG"); importer:SetFrameLevel(160)
        importer:SetBackdrop({bgFile = "Interface/Buttons/WHITE8X8", edgeFile = "Interface/Buttons/WHITE8X8", edgeSize = 1})
        importer:SetBackdropColor(0.05, 0.07, 0.1, 0.98); importer:SetBackdropBorderColor(0.2, 0.6, 0.65, 1)
        local importClose = CreateFrame("Button", nil, importer, "UIPanelCloseButton"); importClose:SetPoint("TOPRIGHT", -2, -2)
        local importTitle = importer:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
        importTitle:SetPoint("TOPLEFT", 20, -20); importTitle:SetText(BLGText("Import blocklist from desktop"))
        local importHint = importer:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        importHint:SetPoint("TOPLEFT", 20, -52); importHint:SetWidth(535); importHint:SetJustifyH("LEFT")
        importHint:SetText(BLGText("Choose Copy sync code in the desktop app, then paste below with Ctrl+V. Only list data is parsed; pasted content is not executed. Codes contain player names; keep them private."))
        local importScroll = CreateFrame("ScrollFrame", nil, importer, "UIPanelScrollFrameTemplate")
        importScroll:SetPoint("TOPLEFT", 24, -100); importScroll:SetSize(510, 220)
        local importText = CreateFrame("EditBox", "BianLingoBridgeBlockImportText", importScroll)
        importText:SetMultiLine(true); importText:SetAutoFocus(false); importText:SetFontObject("ChatFontNormal")
        importText:SetWidth(495); importText:SetHeight(220); importText:SetMaxLetters(BLGBlocklist.MAX_IMPORT_BYTES)
        importScroll:SetScrollChild(importText)
        importText:SetScript("OnEscapePressed", function() importer:Hide() end)
        importText:SetScript("OnTextChanged", function(self)
            local _, lines = self:GetText():gsub("\n", "")
            self:SetHeight(math.max(220, math.min(8200, (lines + 2) * 16)))
        end)
        local importStatus = importer:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        importStatus:SetPoint("TOPLEFT", 20, -340); importStatus:SetWidth(535); importStatus:SetJustifyH("LEFT")
        local apply = CreateFrame("Button", "BianLingoBridgeBlockImportApply", importer, "UIPanelButtonTemplate")
        apply:SetSize(180, 28); apply:SetPoint("BOTTOMLEFT", 20, 20); apply:SetText(BLGText("Import and apply"))
        apply:SetScript("OnClick", function()
            local ok, changed, count = pcall(BLGBlocklist.ImportCode, Config(), importText:GetText())
            if not ok then importStatus:SetText(tostring(changed)); return end
            Refresh(); ExportBlocklist(); importer:Hide()
            p.status:SetText(BLGText("Merged ") .. count .. BLGText(" records, including removals; applied immediately. WoW saves on /reload or normal exit."))
        end)
        importer:SetScript("OnHide", function() importText:ClearFocus(); importText:SetText("") end)
        importer:Hide()
        local import = CreateFrame("Button", "BianLingoBridgeBlockImportOpen", p, "UIPanelButtonTemplate")
        import:SetSize(165, 28); import:SetPoint("TOPLEFT", 360, -400); import:SetText(BLGText("Import from desktop…"))
        import:SetScript("OnClick", function() importStatus:SetText(""); p:Hide(); importer:Show(); importText:SetFocus() end)
        p:SetScript("OnShow", Refresh)
        p:SetScript("OnHide", function() if importer:IsShown() then importer:Hide() end; p.input:ClearFocus() end)
        importer:SetScript("OnHide", function() importText:ClearFocus(); p:Show() end)
        UISpecialFrames = UISpecialFrames or {}; UISpecialFrames[#UISpecialFrames + 1] = "BianLingoBridgeBlocklist"
        UISpecialFrames[#UISpecialFrames + 1] = "BianLingoBridgeBlockImport"
    end
    if blockPanel.importer and blockPanel.importer:IsShown() then blockPanel.importer:Hide() end
    blockPanel:Show()
    local show = blockPanel:GetScript("OnShow"); if show then show() end
end
local function RegisterSettings()
    if optionsPanel then return end
    optionsPanel = CreateFrame("Frame", "BianLingoBridgeOptions", UIParent)
    optionsPanel.name = "BianLingo"
    BuildSettings(optionsPanel, "BianLingoBridgeOptions")
    optionsPanel:Hide()
    if type(Settings) == "table" and type(Settings.RegisterCanvasLayoutCategory) == "function"
        and type(Settings.RegisterAddOnCategory) == "function" then
        local category = Settings.RegisterCanvasLayoutCategory(optionsPanel, optionsPanel.name)
        Settings.RegisterAddOnCategory(category)
    elseif type(InterfaceOptions_AddCategory) == "function" then
        InterfaceOptions_AddCategory(optionsPanel)
    end
end

frame:RegisterEvent("PLAYER_LOGIN")
frame:SetScript("OnEvent", function(_, event, text, author, language, channelName, target, flags, zoneID, channelIndex, channelBaseName, unused, lineID, guid)
    if event ~= "PLAYER_LOGIN" and not enabled then return end
    if event == "PLAYER_LOGIN" then
        if type(BLGBlocklist) ~= "table" then
            Say(BLGText("The blocklist module is missing; install the complete BianLingo folder."))
            enabled = false; frame:Hide(); marker:Hide(); return
        end
        Config()
        seq = 0
        Layout()
        UpdateReplayButton()
        local registered = pcall(RegisterSettings)
        if not registered then Say(BLGText("Could not register the settings page; use /bl settings.")) end
        SetEnabled(Config().enabled, true)
        pcall(frame.UnregisterEvent, frame, "PLAYER_LOGIN")
        if enabled and #BLGBlocklist.Snapshot(Config()).entries > 0 then ExportBlocklist() end
    elseif event == "ADDON_RESTRICTION_STATE_CHANGED" or event == "ENCOUNTER_END" or event == "CHALLENGE_MODE_COMPLETED" or event == "PVP_MATCH_COMPLETE" or event == "PLAYER_ENTERING_WORLD" then
        RefreshChatState()
    elseif event == "UI_SCALE_CHANGED" or event == "DISPLAY_SIZE_CHANGED" then
        Layout(); PositionReplayButton()
    else
        if event == "CHAT_MSG_CHANNEL" then
            local number, name = "", ""
            if not Secret(channelIndex) and type(channelIndex) == "number" then
                number = tostring(channelIndex)
            end
            if Readable(channelBaseName) then name = channelBaseName:gsub("[%z\t\r\n]", " ") end
            event = event .. "\t" .. number .. "\t" .. name
        end
        Enqueue(event, text, author, guid)
    end
end)
SLASH_BIANLINGO1 = "/bl"
SLASH_BIANLINGO2 = "/bianlingo"
SlashCmdList.BIANLINGO = function(input)
    local command, argument = input:lower():match("^%s*(%S*)%s*(.-)%s*$")
    if command == "off" then
        SetEnabled(false)
    elseif command == "on" then
        SetEnabled(true)
    elseif command == "minimap" then
        if argument == "off" then Config().showReplayButton = false
        else Config().showReplayButton = true end
        if argument == "reset" then Config().minimapAngle = 225 end
        UpdateReplayButton(); UpdatePanel()
    elseif command == "settings" or command == "" then
        ShowSettings()
    elseif command == "windows" then
        ExportChatWindows()
    elseif command == "blocks" then
        ShowBlocklist()
    elseif command == "blocksync" then
        ExportBlocklist()
    elseif command == "replay" then
        StartReplay(argument)
    elseif command == "cache" then
        if SetLimit(argument) then Say(BLGText("Cache limit set to ") .. Config().cacheLimit .. BLGText(" messages.")) end
    elseif command == "hide" and (argument == "on" or argument == "off") then
        Config().idleHide = argument == "on"; Appearance()
    elseif command == "mode" and (argument == "native" or argument == "compact") then
        SetMode(argument); Say(BLGText("Pixel mode: ") .. (argument == "compact" and BLGText("Compact matrix") or BLGText("Black & white (HDR compatible)")))
    elseif command == "test" then
        Enqueue("BRIDGE_TEST", "LFM Deadmines, need a healer. Can you summon me?", "local-test")
        DEFAULT_CHAT_FRAME:AddMessage(BLGText("[BianLingo] Test message sent; check the translation overlay."))
    elseif command == "cn" and (argument == "on" or argument == "off") then
        BianLingoDB = BianLingoDB or {}
        BianLingoDB.skipChinese = argument == "on"
        DEFAULT_CHAT_FRAME:AddMessage(BLGText("[BianLingo] Skip Chinese input: ") .. (argument == "on" and BLGText("Enabled") or BLGText("Disabled")))
    elseif command == "size" and (argument == "2" or argument == "3" or argument == "4" or (argument == "1" and Config().mode == "compact")) then
        BianLingoDB.cell = tonumber(argument); Layout()
    elseif command == "corner" and (argument == "tl" or argument == "tr" or argument == "bl" or argument == "br") then
        BianLingoDB.corner = argument; Layout()
    else
        DEFAULT_CHAT_FRAME:AddMessage(string.format(
            BLGText("[BianLingo] settings; blocks; blocksync; windows; minimap on/off/reset; mode native/compact; replay [1–100]; cache [1–100]; hide on/off; test; on/off; cn on/off; size 2/3/4; corner tl/tr/bl/br. Messages %d, queued %d, secret %d, dropped %d, filtered %d, redraws %d, cached %d/%d, not queued %d."),
            seq, QueueSize(), skipped, dropped, filtered, draws, #cache, Config().cacheLimit, unsent))
    end
end
