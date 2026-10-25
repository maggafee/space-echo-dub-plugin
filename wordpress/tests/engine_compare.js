const D=require(require('path').join(__dirname,'../dub-echo/assets/js/dub-echo-engine.js'));
const fs=require('fs');const sr=48000,n=sr*12,block=128;
const raw=new Float32Array(fs.readFileSync('in.f32').buffer.slice(0));
const L=new Float32Array(n),R=new Float32Array(n);for(let i=0;i<n;i++){L[i]=raw[2*i];R[i]=raw[2*i+1];}
const oL=new Float32Array(n),oR=new Float32Array(n);
const e=new D.TapeEchoEngine();e.prepare(sr);
const t0=Date.now();
for(let s=0;s<n;s+=block){const t=s/sr;
 e.setParams({delaySec:t<3?0.3:(t<5?0.12:0.45),feedback:0.6,mode:t<4?5:(t<7?11:8),stereoMode:t<2?1:(t<4?3:(t<8?2:0)),wowFlutter:0.5,tapeAge:0.6,kick:(t>7.0&&t<7.05),power:!(t>9&&t<10.5),duck:t>8?0.4:0,throwOn:(t>10.6&&t<11),bassDb:3,trebleDb:-2,lowCutHz:90});
 const m=Math.min(block,n-s);e.process(L.subarray(s,s+m),R.subarray(s,s+m),oL.subarray(s,s+m),oR.subarray(s,s+m),m);}
const ms=Date.now()-t0;
const c=new Float32Array(fs.readFileSync('cpp.f32').buffer.slice(0));
for(let sec=0;sec<12;sec++){let d=0,p=0;for(let i=sec*sr;i<(sec+1)*sr;i++){const a=c[2*i]-oL[i],b=c[2*i+1]-oR[i];d+=a*a+b*b;p+=c[2*i]**2+c[2*i+1]**2;}
 console.log(sec,'sig',(10*Math.log10(p/sr/2+1e-30)).toFixed(1),'dB  diff',(10*Math.log10(d/sr/2+1e-30)).toFixed(1),'dB  SNR',(10*Math.log10(p/(d+1e-30))).toFixed(1));}
console.log('render ms',ms,'for 12 s audio');
