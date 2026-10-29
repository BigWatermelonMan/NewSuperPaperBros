"""Read-only US0 evt research helper; emits evidence to stdout, never extracts assets."""
import pathlib,struct,re,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
b=(ROOT/'game/main.dol').read_bytes();r=(ROOT/'game/spm_linked.rel').read_bytes();U=lambda bb,o:struct.unpack_from('>I',bb,o)[0];ss=[(U(b,i*4),U(b,0x48+i*4),U(b,0x90+i*4)) for i in range(18) if U(b,0x90+i*4)]
def read(a):
 if 0x80bf6840<=a<0x80bf6840+len(r):return r,a-0x80bf6840
 for o,v,n in ss:
  if v<=a<v+n:return b,o+a-v
 raise ValueError(hex(a))
def w(a):bb,o=read(a);return U(bb,o)
def st(a):
 try:
  bb,o=read(a);s=bb[o:o+100].split(b'\0')[0];return s.decode() if re.fullmatch(rb'[ -~]{2,}',s) else ''
 except:return ''
def dump(a,z):
 p=a
 while p<z:
  v=w(p);n=v>>16;op=v&65535
  if n<=20 and 0<op<=0x78:
   print(hex(p),hex(op),' '.join(hex(w(p+4+i*4))+('='+st(w(p+4+i*4)) if st(w(p+4+i*4)) else '') for i in range(n)));p+=4+n*4
  else:p+=4
def verify():
 # Small evidence fixtures against the user's US0 files. No extracted assets
 # or decompiled code are written by this helper.
 assert w(0x80deed88)==0x10026 and w(0x80deed8c)==411
 assert w(0x80deefa4)==0x10026 and w(0x80deefa8)==415
 assert w(0x80def09c)==0x10026 and w(0x80def0a0)==416
 assert w(0x80deee58)==0x10026 and w(0x80deee5c)==412
 assert [w(0x80deb870+i*4) for i in range(3)]==[0x20032,0xf5de0180,410]
 assert [w(0x80dee424+i*4) for i in range(3)]==[0x20032,0xf5de0180,415]
 assert [w(0x80ded938+i*4) for i in range(3)]==[0x20032,0xf5de0180,416]
 assert [w(0x80dee328+i*4) for i in range(5)]==[0x4005c,0x801034c8,0xfe363c8d,9,0x80ded9a0]
 assert w(0x803fc290+309*0x68)==0
 assert w(0x803fc290+305*0x68+0x18)>>24==150
 assert w(0x803fc290+309*0x68+0x18)>>24==200
 assert st(0x80c9d4d0)=='BGM_BTL_MR_ZIGEN1'
 assert st(0x80c9d1cc)=='BGM_BTL_BOSS_LAST1'
 assets=ROOT/'Disc/DATA/files/a'
 expected={'e_jigen':['S_1','A_1a','A_1b','A_1c','A_4a','A_4b','A_4c','A_4g'],
  'e_DmenL_k':['S_1','A_2b'],'e_DmenL_f':['S_1','A_1'],
  'e_DmenL_lh':['S_1','A_1b'],'e_DmenL_rh':['S_1','A_1b'],
  'e_DmenL_s':['S_1'],'e_DmenL_r':['S_1'],
  'p_dot_luigi':['S_1','J_1'],'FRY_mekri':['S_1']}
 for model,animations in expected.items():
  data=(assets/model).read_bytes()
  for animation in animations:assert animation.encode()+b'\0' in data,(model,animation)
 print('PASS: US0 story branches, handoff, HP, music, native models and animations')
if __name__=='__main__':
 if sys.argv[1:] == ['--verify']:verify()
 elif len(sys.argv)==3:dump(int(sys.argv[1],16),int(sys.argv[2],16))
 else:raise SystemExit('Usage: lp-final-boss-research.py --verify | START_HEX END_HEX')
