"""Execute the shipped WoW sender in real Lua 5.1 (developer dependency: lupa)."""
import argparse
import base64
import json
import struct
import zlib
from pathlib import Path
from lupa.lua51 import LuaRuntime
ROOT = Path(__file__).resolve().parents[1]
MOCK = r"""
frames, messages, mutations = {}, {}, 0
local methods = {}
function methods:SetScript(k,v) self.scripts[k]=v end
function methods:GetScript(k) return self.scripts[k] end
function methods:RegisterEvent(k) self.events[k]=true end
function methods:UnregisterEvent(k) self.events[k]=nil end
function methods:CreateTexture() local t=NewObject(); self.textures[#self.textures+1]=t; return t end
function methods:CreateFontString() return NewObject() end
function methods:SetColorTexture(r,g,b,a) self.rgb={r,g,b,a}; mutations=mutations+1 end
function methods:SetText(v) self.text=tostring(v) end
function methods:GetText() return self.text or '' end
function methods:SetChecked(v) self.checked=v end
function methods:GetChecked() return self.checked end
function methods:Show() local before=self.shown;self.shown=true;if not before and self.scripts.OnShow then self.scripts.OnShow(self) end end
function methods:Hide() self.shown=false;if self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:SetShown(v) if v then self:Show() else self:Hide() end end
function methods:GetCenter() return 800,800 end
function methods:GetWidth() return rawget(self,"w") or 140 end
function methods:GetHeight() return rawget(self,"h") or 140 end
function methods:GetFrameLevel() return 2 end
function GetCursorPosition() return cursorX or 920,cursorY or 800 end
function methods:GetLeft() return 123 end
function methods:GetBottom() return 234 end
function methods:IsShown() return self.shown end
function methods:SetScale(v) self.scale=v end
function methods:GetScale() return self.scale or 1 end
function methods:GetEffectiveScale() return 1 end
function methods:SetPoint(...) self.point={...} end
function methods:SetSize(w,h) self.w=w;self.h=h end
function methods:AddMessage(s) messages[#messages+1]=s end
function NewObject() return setmetatable({scripts={},events={},textures={},shown=true}, {__index=function(_,k) return methods[k] or function() end end}) end
function CreateFrame(_,name,parent,template) local f=NewObject(); f.parent=parent; if name then frames[name]=f; _G[name]=f end; return f end
Minimap=NewObject();GameTooltip=NewObject(); UIParent=NewObject(); DEFAULT_CHAT_FRAME=NewObject(); SlashCmdList={}; UISpecialFrames={}
now=1792880000; queries=0
function GetTime() return 123456 end
function GetServerTime() return now end
function GetPhysicalScreenSize() return 1920,1080 end
function issecretvalue(v) return v=='SECRET' end
Settings={
 RegisterCanvasLayoutCategory=function(frame,name) registeredSettings=frame; return {name=name} end,
 RegisterAddOnCategory=function(category) registeredCategory=category end
}
function GetPlayerInfoByGUID(guid) queries=queries+1; return '猎人','HUNTER','人类','Human',2,'Player','Realm' end
"""

def boot(saved=None, uptime=123456, interface=16001, secret_api=True, home_realm=None, fallback_realm=None, locale="enUS"):
    lua=LuaRuntime(encoding=None)
    lua.execute(MOCK.encode())
    lua.globals().testLocale = locale.encode()
    lua.execute(b'function GetLocale() return testLocale end')
    lua.execute(f"function GetTime() return {uptime} end".encode())
    lua.execute(f"function GetBuildInfo() return 'test', '70003', 'Oct 2026', {interface} end".encode())
    lua.execute(b'realm_queries=0')
    if home_realm is not None:
        lua.globals().testHomeRealm=home_realm.encode()
        lua.execute(b'function GetNormalizedRealmName() realm_queries=realm_queries+1;return testHomeRealm end')
    if fallback_realm is not None:
        lua.globals().testFallbackRealm=fallback_realm.encode()
        lua.execute(b'function GetRealmName() realm_queries=realm_queries+1;return testFallbackRealm end')
    if not secret_api: lua.globals().issecretvalue=None
    if saved is not None:
        lua.globals().BianLingoDB=lua.table_from(saved, recursive=True)
    lua.execute((ROOT/'addon/BianLingo/LocaleMessages.lua').read_bytes())
    lua.execute((ROOT/'addon/BianLingo/TraditionalMarkers.lua').read_bytes())
    lua.execute((ROOT/'addon/BianLingo/Blocklist.lua').read_bytes())
    lua.execute((ROOT/'addon/BianLingo/PixelBridge.lua').read_bytes())
    return Sender(lua)

class Sender:
    def __init__(self,lua):
        self.lua=lua; self.g=lua.globals();self.frame=self.g.BianLingoBridgeFrame;self.marker=self.g.BianLingoIdleMarker
        self.g.BianLingoBridgeFrame[b'scripts'][b'OnEvent'](self.frame,b'PLAYER_LOGIN')
        self.packets=[];self.drain()
    def command(self,s): self.g.SlashCmdList[b'BIANLINGO'](s.encode())
    def chat(self,text,author='Player-Realm',guid='Player-123'):
        f=self.frame[b'scripts'][b'OnEvent']
        f(self.frame,b'CHAT_MSG_SAY',text.encode(),author.encode(),*([None]*9),guid.encode())
    def raw(self):
        if self.g.BianLingoDB[b'mode']==b'compact':
            planes=['', '', '']
            for i in range(360):
                x=2+i//10;row=i%10;y=9-row if x%2 else row
                t=self.frame[b'textures'][2+y*40+x];rgb=t[b'rgb']
                for k in range(3):
                    value=round(rgb[k+1]*7) if t[b'shown'] else 0
                    planes[k]+=f'{value:03b}'
            bits=''.join(planes)
            wire=bytes(int(bits[i:i+8],2) for i in range(0,3240,8))
            assert wire[:4]==b'MCT1' and wire[-1]==0
            raw=wire[8:404]
            assert zlib.adler32(raw)==int.from_bytes(wire[4:8],'big')
            return raw
        values=[]
        for i in range(98,1154): # textures[1] background, textures[2..97] sync
            t=self.frame[b'textures'][i];rgb=t[b'rgb'];v=0
            for k in range(1,4): v=v*2+int(bool(t[b'shown']) and rgb[k]>.5)
            values.append(v)
        bits=''.join(f'{v:03b}' for v in values)
        return bytes(int(bits[i:i+8],2) for i in range(0,len(bits),8))
    def image_fixture(self):
        compact=self.g.BianLingoDB[b'mode']==b'compact'
        cols,rows=(40,10) if compact else (96,12)
        cell=int(self.g.BianLingoDB[b'cell']);w,h=cols*cell,rows*cell
        data=bytearray(w*h*4)
        for y in range(h):
            for x in range(w):
                t=self.frame[b'textures'][2+(y//cell)*cols+x//cell];rgb=t[b'rgb']
                i=(y*w+x)*4
                data[i:i+4]=bytes([round(rgb[k]*255) if t[b'shown'] else 0 for k in (3,2,1)]+[255])
        return dict(width=w,height=h,cell=cell,compact=compact,pixels=base64.urlsafe_b64encode(data).decode().rstrip("="),raw=self.raw().hex())
    def marker_raw(self):
        bits=''
        for i in range(1,17):
            c=self.marker[b'textures'][i][b'rgb'];bits+=''.join(str(int(c[k]>.5)) for k in range(1,4))
        return bytes(int(bits[i:i+8],2) for i in range(0,48,8))
    def tick(self):
        fn=self.frame[b'scripts'][b'OnUpdate']
        if fn:
            fn(self.frame,.2)
            raw=self.raw()
            if raw[:4]==b'BLG2': self.packets.append(parse(raw))
        return bool(fn)
    def drain(self):
        for _ in range(3000):
            if not self.tick(): return
        raise AssertionError('sender never became idle')
    def snapshot(self,batch=None):
        out={}
        for p in self.packets:
            if p['batch'] and (batch is None or p['batch']==batch):
                out.setdefault(p['seq'],{})[p['part']]=p
        return {seq:b''.join(v[i]['body'] for i in sorted(v)) for seq,v in out.items()}

def parse(raw):
    session,seq,sent,part,count,length,event,player=struct.unpack('>IIIHHHBB',raw[4:24])
    data=raw[28:28+length]
    assert zlib.adler32(raw[:24]+data)==int.from_bytes(raw[24:28],'big')
    batch=int.from_bytes(data[:2],'big') if event&128 else 0
    return dict(raw=raw.hex(),session=session,seq=seq,sent=sent,part=part,count=count,event=event&127,player=player,
                batch=batch,body=data[2:] if batch else data)

def realm_fixtures():
    output=[]
    # Full identity stays in author and logs. Only the whisper prefix may shorten.
    cases=[('Realm',None,'Player-Realm','Realm','Player-123',True),
           ('R e a l m',None,'Player-REALM','Realm','Player-123',True),
           ('Realm',None,'Player','Realm','Player-123',True),
           ('Other',None,'Player-Realm','Realm','Player-123',False),
           (None,None,'Player-Realm','Realm','Player-123',False),
           ('',None,'Player-Realm','Realm','Player-123',False),
           ('SECRET',None,'Player-Realm','Realm','Player-123',False),
           ('Realm',None,'Player-OtherRealm','Realm','Player-123',False),
           ('Realm',None,'Player-Realm','OtherRealm','Player-123',False),
           ('Realm',None,'Player-Realm','Realm','',True),
           (None,'Realm','Player-Realm','Realm','Player-123',True),
           ('SECRET','Realm','Player-Realm','Realm','Player-123',True)]
    for mode in [b'native',b'compact']:
        for index,(home,fallback,author,metadata,guid,same) in enumerate(cases):
            sender=boot({b'mode':mode},uptime=124000+index,home_realm=home,fallback_realm=fallback)
            sender.g.testPlayerRealm=metadata.encode()
            sender.lua.execute(b"function GetPlayerInfoByGUID(guid) queries=queries+1; return 'hunter','HUNTER','human','Human',2,'Player',testPlayerRealm end")
            realm_queries=sender.g.realm_queries;sender.packets=[]
            sender.chat('same realm probe',author,guid);sender.tick()
            first=sender.packets[-1];assert first['event']==1 and bool(first['player']&32)==same
            image=sender.image_fixture() if index==0 else None
            sender.drain();queries=sender.g.queries
            sender.packets=[];sender.command('replay');sender.drain()
            replay=next(p for p in sender.packets if p['batch'])
            assert replay['player']==first['player'] and replay['body']==first['body']
            assert sender.g.queries==queries and sender.g.realm_queries==realm_queries
            sender.chat('cached realm check',author,guid);sender.drain()
            assert sender.g.queries==queries and sender.g.realm_queries==realm_queries
            expected_name='Player' if same else author if '-' in author else author+'-'+metadata
            fixture=dict(raw=first['raw'],replay_raw=replay['raw'],same_realm=same,
                         author=author,prefix='/w '+expected_name+' ',player_class='HUNTER' if guid else '')
            if image is not None: fixture['image']=image
            output.append(fixture)
        fallback=boot({b'mode':mode},interface=11509,secret_api=False,home_realm='Realm')
        fallback.packets=[];fallback.chat('classic era local realm');fallback.tick()
        first=fallback.packets[-1];assert first['player']&32 and fallback.g.realm_queries==1
        fallback.drain();fallback.packets=[];fallback.command('replay');fallback.drain()
        replay=next(p for p in fallback.packets if p['batch'])
        assert replay['player']==first['player'] and fallback.g.realm_queries==1 and fallback.g.queries==1
        output.append(dict(raw=first['raw'],replay_raw=replay['raw'],same_realm=True,
                           author='Player-Realm',prefix='/w Player ',player_class='HUNTER'))
    return output

def layout_export_fixtures():
    output=[]
    setup=r"""
layout_calls=0;NUM_CHAT_WINDOWS=4
ChatTypeGroup={SAY={"CHAT_MSG_SAY"},PARTY={"CHAT_MSG_PARTY","CHAT_MSG_PARTY_LEADER"},WHISPER={"CHAT_MSG_WHISPER","CHAT_MSG_WHISPER_INFORM"},GUILD={"CHAT_MSG_GUILD","CHAT_MSG_OFFICER"},COMBAT={"CHAT_MSG_COMBAT_LOG"},CHANNEL={"CHAT_MSG_CHANNEL"}}
function GetChatWindowInfo(index)
 layout_calls=layout_calls+1
 local names={"综合"..string.char(34).."测试"..string.char(92).." éΩ😀","私聊","战斗记录","交易"}
 return names[index],14,0,0,0,0,true
end
function GetChatWindowMessages(index)
 layout_calls=layout_calls+1
 if index==1 then return "SAY","PARTY","WHISPER","GUILD" end
 if index==2 then return "WHISPER" end
 if index==3 then return "COMBAT" end
 return "CHANNEL"
end
function GetChatWindowChannels(index)
 layout_calls=layout_calls+1
 if index==1 then return "General",1,"Trade - Westfall",2 end
 if index==4 then return "Trade - Westfall",2 end
end
"""
    for mode in [b'native',b'compact']:
        sender=boot({b'mode':mode},uptime=123469)
        sender.lua.execute(setup.encode());sender.packets=[]
        queries=sender.g.queries;sender.command('windows');sender.tick()
        calls=sender.g.layout_calls
        assert calls==12 and sender.g.queries==queries
        sender.command('windows')
        assert sender.g.layout_calls==calls # No duplicate active snapshot.
        sender.chat('live priority during layout')
        sender.tick();assert sender.packets[-1]['event']==1
        queries=sender.g.queries;sender.drain()
        assert sender.g.layout_calls==calls and sender.g.queries==queries
        unique={p['part']:p for p in sender.packets if p['event']==22}
        assert len(unique)>1 and len(unique)==next(iter(unique.values()))['count']
        body=b''.join(unique[i]['body'] for i in sorted(unique))
        assert body[:1]==b'\0'
        layout=json.loads(body[1:])
        assert [t['id'] for t in layout['windows']]==['wow-1','wow-2','wow-4']
        assert layout['windows'][0]['name']=='综合"测试'+chr(92)+' éΩ😀'
        assert layout['windows'][1]['events']==['CHAT_MSG_WHISPER','CHAT_MSG_WHISPER_INFORM']
        assert layout['windows'][2]['channels']==['Trade - Westfall']
        output.append(dict(packets=[unique[i]['raw'] for i in sorted(unique)],expected=layout))
        sender.packets=[];sender.command('replay');sender.drain()
        assert not any(p['event']==22 for p in sender.packets) # Layout is not chat history.
        stable=sender.g.layout_calls
        for _ in range(30):sender.tick()
        assert sender.g.layout_calls==stable
        sender.lua.execute(b'function GetChatWindowInfo() return "SECRET",14,0,0,0,0,true end')
        sender.packets=[];sender.command('windows');sender.drain()
        assert not any(p['event']==22 for p in sender.packets)
        sender.g.GetChatWindowChannels=None;sender.command('windows');sender.drain()
        assert not any(p['event']==22 for p in sender.packets)
    return output

def crc8(data):
    n=0
    for b in data:
        n^=b
        for _ in range(8): n=((n<<1)^(7 if n&128 else 0))&255
    return n

def run(output=None):
    markers=[];images=[];colors=[]
    fresh=boot({})
    assert fresh.g.BianLingoDB[b'idleHide'] is True
    assert fresh.g.BianLingoDB[b'enabled'] is True
    fresh.command('settings')
    switch=fresh.g.BianLingoBridgeSettingsEnabled
    switch.SetChecked(switch,False);switch[b'scripts'][b'OnClick'](switch)
    assert fresh.g.BianLingoDB[b'enabled'] is False
    assert not fresh.frame[b'events'][b'CHAT_MSG_SAY'] and not fresh.frame[b'scripts'][b'OnUpdate']
    assert not fresh.frame[b'shown'] and not fresh.marker[b'shown']
    before=fresh.g.queries;fresh.chat('disabled message');fresh.tick()
    assert fresh.g.queries==before and not fresh.frame[b'scripts'][b'OnUpdate']
    switch.SetChecked(switch,True);switch[b'scripts'][b'OnClick'](switch)
    assert fresh.frame[b'events'][b'CHAT_MSG_SAY'] and fresh.g.BianLingoDB[b'enabled']
    saved_off=boot({b'enabled':False})
    assert not saved_off.frame[b'events'][b'CHAT_MSG_SAY'] and not saved_off.frame[b'shown']
    saved_off.chat('must not capture');assert not saved_off.tick()

    fresh.command('settings')
    assert fresh.g.BianLingoBridgeSettings[b'shown']
    fresh.command('blocks')
    assert fresh.g.BianLingoBridgeBlocklist[b'shown'] and not fresh.g.BianLingoBridgeSettings[b'shown']
    opener=fresh.g.BianLingoBridgeBlockImportOpen
    opener[b'scripts'][b'OnClick'](opener)
    assert fresh.g.BianLingoBridgeBlockImport[b'shown'] and not fresh.g.BianLingoBridgeBlocklist[b'shown']
    importer=fresh.g.BianLingoBridgeBlockImport
    importer.Hide(importer)
    assert fresh.g.BianLingoBridgeBlocklist[b'shown'] and not importer[b'shown']
    fresh.command('settings')
    assert fresh.g.BianLingoBridgeSettings[b'shown'] and not fresh.g.BianLingoBridgeBlocklist[b'shown']
    # Game options registration exposes working controls without slash commands.
    controls=boot()
    assert controls.g.registeredCategory[b'name']=='BianLingo'.encode()
    controls.g.registeredSettings.Show(controls.g.registeredSettings)
    idle=controls.g.BianLingoBridgeOptionsIdleHide
    idle.SetChecked(idle,True);idle[b'scripts'][b'OnClick'](idle)
    assert controls.g.BianLingoDB[b'idleHide'] and controls.marker[b'shown']
    limit=controls.g.BianLingoBridgeOptionsCacheLimit
    limit.SetText(limit,b'7');save=controls.g.BianLingoBridgeOptionsSave
    save[b'scripts'][b'OnClick'](save);assert controls.g.BianLingoDB[b'cacheLimit']==7
    controls.chat('hello options replay');controls.drain();controls.packets=[]
    replay=controls.g.BianLingoBridgeOptionsReplay
    replay[b'scripts'][b'OnClick'](replay);controls.drain();assert len(controls.snapshot())==1
    controls.command('settings');popup=controls.g.BianLingoBridgeSettingsIdleHide
    assert popup[b'checked']
    compact_radio=controls.g.BianLingoBridgeOptionsCompactMode
    compact_radio[b'scripts'][b'OnClick'](compact_radio)
    assert controls.g.BianLingoDB[b'mode']==b'compact' and controls.frame[b'w']==42
    native_radio=controls.g.BianLingoBridgeOptionsNativeMode
    native_radio[b'scripts'][b'OnClick'](native_radio)
    assert controls.g.BianLingoDB[b'mode']==b'native' and controls.frame[b'w']==196
    # Ring-attached minimap button: defaults visible, left settings / right replay.
    button=controls.g.BianLingoBridgeReplayButton
    assert button[b'shown'] and not button[b'scripts'][b'OnUpdate']
    assert button[b'point'][1]==b'CENTER' and button[b'parent'][b'scripts'] is not None
    toggle=controls.g.BianLingoBridgeOptionsShowReplayButton
    controls.packets=[];q=controls.g.queries
    button[b'scripts'][b'OnClick'](button,b'LeftButton');assert controls.g.BianLingoBridgeSettings[b'shown']
    assert not controls.snapshot() and controls.g.queries==q
    button[b'scripts'][b'OnClick'](button,b'RightButton');controls.drain()
    assert controls.snapshot() and controls.g.queries==q
    button[b'scripts'][b'OnDragStart'](button)
    assert button[b'scripts'][b'OnUpdate']
    controls.lua.execute(b'cursorX=800;cursorY=920')
    button[b'scripts'][b'OnUpdate'](button,.1)
    button[b'scripts'][b'OnDragStop'](button)
    assert not button[b'scripts'][b'OnUpdate'] and abs(controls.g.BianLingoDB[b'minimapAngle']-90)<.001
    assert abs(button[b'point'][4])<.001 and abs(button[b'point'][5]-78)<.001
    restored=boot({b'showReplayButton':True,b'minimapButtonV2':True,b'minimapAngle':90})
    assert restored.g.BianLingoBridgeReplayButton[b'shown'] and abs(restored.g.BianLingoBridgeReplayButton[b'point'][5]-78)<.001
    toggle.SetChecked(toggle,False);toggle[b'scripts'][b'OnClick'](toggle);assert not button[b'shown']
    controls.command('minimap reset');assert button[b'shown'] and controls.g.BianLingoDB[b'minimapAngle']==225
    controls.command('minimap off');assert not button[b'shown'] and not button[b'scripts'][b'OnUpdate']
    assert not boot({b'showReplayButton':False,b'minimapButtonV2':True}).g.BianLingoBridgeReplayButton[b'shown']
    # Exact native channel colors: 3 binary bytes, no extra player lookup on replay.
    colored=boot();colored.lua.execute(b'ChatTypeInfo={SAY={r=0.25,g=1,b=0.75},CHANNEL1={r=0.1,g=0.2,b=0.3}}')
    colored.packets=[];colored.chat('|cff0070dd|Hitem:1|h[Blue Item]|h|r free at the inn');colored.drain()
    colorpacket=next(p for p in colored.packets if p['event']==1)
    assert colorpacket['player']&64 and colorpacket['body'][:3]==bytes([64,255,191])
    colors.append(dict(raw=colorpacket['raw'],rgb=0x40ffbf,text='|cff0070dd|Hitem:1|h[Blue Item]|h|r free at the inn'))
    q=colored.g.queries;colored.packets=[];colored.command('replay');colored.drain()
    assert colored.g.queries==q and all(body[:3]==bytes([64,255,191]) for body in colored.snapshot().values())
    f=colored.frame[b'scripts'][b'OnEvent']
    f(colored.frame,b'CHAT_MSG_CHANNEL',b'need a healer',b'Player-Realm',None,b'General',None,None,None,1,b'General',None,None,b'Player-123')
    colored.drain();cp=next(p for p in colored.packets if p['event']==14)
    assert cp['body'].startswith(bytes([26,51,77])+b'1\tGeneral\0')
    colors.append(dict(raw=cp['raw'],rgb=0x1a334d,text='need a healer'))
    colored.lua.execute(b'ChatTypeInfo.SAY.r="SECRET"');colored.packets=[]
    colored.chat('secret color falls back');colored.drain()
    assert not(next(p for p in colored.packets if p['event']==1)['player']&64)

    # Supported client declarations and fallback API compatibility.
    toc=(ROOT/'addon/BianLingo/BianLingo.toc').read_text(encoding='utf-8')
    supported={11509,16001,20506,30403,40402,50504,120100,120105}
    declared={int(value.strip()) for value in toc.splitlines()[0].split(':',1)[1].split(',')}
    assert declared==supported
    era=boot(interface=11509,secret_api=False)
    era.chat('hello from classic era');era.drain()
    assert any(p['event']==1 for p in era.packets)
    channel=era.frame[b'scripts'][b'OnEvent']
    channel(era.frame,b'CHAT_MSG_CHANNEL',b'need a healer',b'Player-Realm',None,b'General',None,None,None,1,b'General',None,None,b'Player-123')
    era.drain();channel_packet=next(p for p in era.packets if p['event']==14)
    assert channel_packet['body'].startswith(b'1\tGeneral\0') and channel_packet['player']&127==3
    era.packets=[];era.command('replay');era.drain();assert len(era.snapshot())==2
    era.packets=[];era.chat('来个 DPS');era.chat('a123!!');era.drain();assert not era.packets
    # Classic Era, Season of Discovery, Anniversary and expansion clients
    # can run without the retail-only secret-value API.
    for interface in [11509,20506,30403,40402,50504]:
        classic=boot(interface=interface,secret_api=False)
        classic.packets=[];classic.chat('classic expansion chat');classic.drain()
        assert any(p['event']==1 for p in classic.packets), interface
    unsupported=boot(interface=16001,secret_api=False);unsupported.chat('must fail closed');assert not unsupported.tick()
    retail=boot(interface=120105,secret_api=False);retail.chat('must fail closed');assert not retail.tick()
    unknown=boot(interface=99999,secret_api=False);unknown.chat('must fail closed');assert not unknown.tick()
    for interface in supported:
        protected=boot(interface=interface);protected.chat('SECRET');assert not protected.tick()
    # Restriction marker is persistent and contains state only, never message bodies.
    stateful=boot();stateful.packets=[]
    stateful.lua.execute(b'C_ChatInfo={InChatMessagingLockdown=function() return lockdown or false end}')
    event=stateful.frame[b'scripts'][b'OnEvent']
    stateful.g.lockdown=True
    event(stateful.frame,b'ADDON_RESTRICTION_STATE_CHANGED')
    stateful.chat('SECRET');stateful.drain()
    assert stateful.marker[b'shown'] and not stateful.packets
    def marker_bytes(sender):
        bits=''.join('1' if sender.marker[b'textures'][n][b'rgb'][c]>.5 else '0' for n in range(1,17) for c in range(1,4))
        return bytes(int(bits[i:i+8],2) for i in range(0,48,8))
    raw=marker_bytes(stateful)
    assert raw[2]>>4==2 and (int.from_bytes(raw[3:5],'big')>>12)&3==2
    markers.append(dict(raw=raw.hex(),corner=0,cell=2,active=False,compact=False,chat_state=2))
    mutations=stateful.g.mutations
    event(stateful.frame,b'ADDON_RESTRICTION_STATE_CHANGED')
    assert stateful.g.mutations==mutations
    stateful.g.lockdown=False
    event(stateful.frame,b'ADDON_RESTRICTION_STATE_CHANGED')
    raw=marker_bytes(stateful)
    assert (int.from_bytes(raw[3:5],'big')>>12)&3==1
    markers.append(dict(raw=raw.hex(),corner=0,cell=2,active=False,compact=False,chat_state=1))
    stateful.chat('recovered chat');stateful.drain();assert stateful.packets
    stateful.command('off');assert not stateful.marker[b'shown']
    s=boot(); assert s.g.BianLingoDB[b'cacheLimit']==10
    for invalid in ['0','101','2.5','abc']: s.command('cache '+invalid);assert s.g.BianLingoDB[b'cacheLimit']==10
    for i in range(15): s.chat(f'hello player {i}')
    s.drain();s.packets=[];q=s.g.queries;s.command('replay');s.command('replay');s.drain()
    assert len(s.snapshot())==10 and s.g.queries==q
    assert any('Replaying'.encode() in v for _,v in s.g.messages.items())
    assert min(s.snapshot())==7 # ready seq1, chats seq2..16, retains7..16
    assert {p['event'] for p in s.packets}>={20,21,1}
    fixtures=list(s.packets)
    # immutable snapshot, with live packet priority at the next boundary
    s.command('cache 100');s.packets=[];s.command('replay 10');s.tick();s.command('cache 1')
    s.g.now+=10;s.chat('live priority message');s.tick()
    assert s.packets[-1]['event']==1 and not s.packets[-1]['batch']
    s.drain();assert len(s.snapshot())==10
    assert all(p['sent']==1792880000 for p in s.packets if p['batch'])
    fixtures+=s.packets
    s.packets=[];s.command('replay 100');s.drain();assert len(s.snapshot())==1
    # persisted limit; RAM messages disappear in a fresh Lua state
    t=boot({b'cacheLimit':1,b'idleHide':True}, uptime=123457); t.command('replay'); assert not t.snapshot()
    # Whole-message byte cap, despite queue overflow (cache-before-send behavior).
    t.command('cache 100')
    for i in range(100):t.chat('long message '+str(i)+' '+('x'*15970))
    t.command('replay 100')
    while not (t.packets and t.packets[-1]['batch']): t.tick()
    previous=t.packets[-1]
    t.g.now+=1;t.chat('high priority during replay');t.tick()
    assert not t.packets[-1]['batch'] and t.packets[-1]['event']==1
    t.tick();assert t.packets[-1]['batch']==previous['batch'] and t.packets[-1]['seq']==previous['seq'] and t.packets[-1]['part']==previous['part']+1
    t.drain();bodies=t.snapshot()
    assert 30<=len(bodies)<=33 and sum(map(len,bodies.values()))<=512*1024
    assert all(len(v)>15970 for v in bodies.values())
    # Per replay packet <=366 body bytes; regular wire protocol preserved.
    assert all(len(p['body'])<=366 for p in t.packets if p['batch'])
    long_seq=min(bodies)
    fixtures += [p for p in t.packets if p['batch'] and p['seq']==long_seq]
    # Traditional/simplified Chinese and noise do not wake the strip.
    t.drain();assert not t.frame[b'shown'] and t.marker[b'shown']
    initial=t.g.mutations
    for text in ['来个 DPS','大家好','歡迎來到這裡 abc','123456','a','a123!!'] : t.chat(text)
    assert not t.tick() and t.g.mutations==initial
    t.command('settings');assert t.g.BianLingoBridgeSettings[b'shown']
    for corner,number in [('tl',0),('tr',1),('bl',2),('br',3)]:
        for cell in [2,3,4]:
            t.command(f'corner {corner}');t.command(f'size {cell}')
            raw=t.marker_raw();assert raw[:2]==b'\xfc\xb1' and raw[5]==crc8(raw[:5])
            assert raw[2]&3==cell-2 and int.from_bytes(raw[3:5],'big')>>14==number
            markers.append(dict(raw=raw.hex(),corner=number,cell=cell,active=False))
    t.chat('new wake message');assert t.frame[b'shown'] and t.marker_raw()[2]&4
    markers.append(dict(raw=t.marker_raw().hex(),corner=3,cell=4,active=True))
    t.tick();t.tick()
    for _ in range(3): t.tick();assert t.frame[b'shown']
    t.tick();assert not t.frame[b'shown']
    # tail redundancy + exact minimum idle hold
    while t.frame[b'scripts'][b'OnUpdate']:
        t.tick()
    assert not t.frame[b'shown'] and not(t.marker_raw()[2]&4)
    stable=t.g.mutations
    for _ in range(50):t.tick()
    assert t.g.mutations==stable
    t.command('hide off');assert t.frame[b'shown'] and not t.marker[b'shown']
    t.command('hide on');assert not t.frame[b'shown'] and t.marker[b'shown']
    t.command('off');assert not t.marker[b'shown'] and not t.frame[b'shown']
    # Dense mode reuses the complete BLG2 packet, including metadata/replay IDs.
    short=boot({b'mode':b'compact',b'idleHide':True},uptime=123459)
    assert short.frame[b'w']==42 and short.frame[b'h']==12
    short.chat('compact transport '+('hello '*160));short.tick()
    held=short.raw();images.append(short.image_fixture())
    q=short.g.queries
    short.command('mode native');assert short.raw()==held and short.frame[b'w']==196
    images.append(short.image_fixture())
    short.command('mode compact');assert short.raw()==held and short.g.queries==q
    short.drain();short.packets=[];short.command('replay');short.tick();short.tick()
    assert short.packets[-1]['batch']
    images.append(short.image_fixture())
    short.drain()
    idle_mutations = short.g.mutations
    for _ in range(2000): short.tick()
    assert short.g.mutations == idle_mutations and not short.frame[b'scripts'][b'OnUpdate']
    short.command('hide off');assert short.frame[b'shown'] and short.marker[b'shown'] and short.marker_raw()[2]&4
    short.command('hide on');assert not short.frame[b'shown'] and not(short.marker_raw()[2]&4)
    for corner in ['tl','tr','bl','br']:
        short.command('corner '+corner)
        for cell in [1,2,3,4]:
            short.command('size '+str(cell));images.append(short.image_fixture())
            markers.append(dict(raw=short.marker_raw().hex(),corner=['tl','tr','bl','br'].index(corner),cell=cell,active=False,compact=True))
    assert not any(short.frame[b'textures'][i][b'shown'] for i in range(402,1154))
    layouts=layout_export_fixtures()
    realms=realm_fixtures()
    for fixture in realms:
        if 'image' in fixture: images.append(fixture.pop('image'))
    if output:
        clean=[{k:v for k,v in p.items() if k!='body'} for p in fixtures]
        Path(output).write_text(json.dumps(dict(packets=clean,markers=markers,images=images,colors=colors,layouts=layouts,realms=realms)),encoding='utf-8')
    print('PASS real Lua 5.1: defaults/persistence, whole cache eviction, immutable replay, live priority, overflow recovery, filters, four corners/cells, idle wake/hide, no idle redraw')
def notification_languages():
    for language in ['enUS', 'frFR', 'deDE', 'jaJP', 'koKR', 'zhCN', 'zhTW']:
        sample = boot(locale=language)
        expected = '同步码格式无效'.encode() if language.startswith('zh') else b'Invalid sync code format'
        assert sample.g.BLGText(b'Invalid sync code format') == expected
        ok, error = sample.g.pcall(sample.g.BLGBlocklist.ImportCode, sample.g.BianLingoDB, b'broken')
        assert not ok and expected in error, 'Addon errors ignored the UI locale'

if __name__=='__main__':
    notification_languages()
    parser=argparse.ArgumentParser();parser.add_argument('--fixtures');run(parser.parse_args().fixtures)
