// Offline EVENT_RECORD/TDH cross-check, x64 ABI only. Never creates/controls a trace.
// Publishes numerical fields, provider metadata and SHA256; no raw payload/path/stack.
using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

namespace MqbOfflineEtl {
public static class Reader {
    [StructLayout(LayoutKind.Explicit, Size=448)]
    struct LogFile {
        [FieldOffset(0)] public IntPtr FileName;
        [FieldOffset(28)] public uint Mode;
        [FieldOffset(120)] public uint BufferSize;
        [FieldOffset(132)] public uint Processors;
        [FieldOffset(156)] public uint BuffersWritten;
        [FieldOffset(164)] public uint PointerSize;
        [FieldOffset(168)] public uint EventsLost;
        [FieldOffset(376)] public long Frequency;
        [FieldOffset(392)] public uint ClockType;
        [FieldOffset(396)] public uint BuffersLost;
        [FieldOffset(424)] public IntPtr Callback;
    }
    [StructLayout(LayoutKind.Explicit, Size=112)]
    struct Record {
        [FieldOffset(4)] public ushort Flags;
        [FieldOffset(8)] public uint Tid;
        [FieldOffset(12)] public uint Pid;
        [FieldOffset(16)] public long Time;
        [FieldOffset(24)] public Guid Provider;
        [FieldOffset(40)] public ushort Id;
        [FieldOffset(42)] public byte Version;
        [FieldOffset(45)] public byte Opcode;
        [FieldOffset(80)] public ushort Cpu;
        [FieldOffset(86)] public ushort Length;
        [FieldOffset(96)] public IntPtr Data;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct Property { public ulong Name; public uint ArrayIndex; public uint Reserved; }
    [UnmanagedFunctionPointer(CallingConvention.Winapi)] delegate void Callback(IntPtr p);
    [DllImport("advapi32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern ulong OpenTraceW(ref LogFile log);
    [DllImport("advapi32.dll")] static extern uint ProcessTrace(ulong[] handles, uint count, IntPtr start, IntPtr end);
    [DllImport("advapi32.dll")] static extern uint CloseTrace(ulong handle);
    [DllImport("tdh.dll")] static extern uint TdhGetEventInformation(IntPtr e,uint n,IntPtr ctx,IntPtr info,ref uint size);
    [DllImport("tdh.dll")] static extern uint TdhGetPropertySize(IntPtr e,uint n,IntPtr ctx,uint count,ref Property prop,out uint size);
    [DllImport("tdh.dll")] static extern uint TdhGetProperty(IntPtr e,uint n,IntPtr ctx,uint count,ref Property prop,uint size,byte[] buffer);
    static readonly HashSet<string> Allowed = new HashSet<string>(StringComparer.Ordinal) {
        "ProcessId","ParentId","ParentProcessId","ExitStatus","ThreadId","TThreadId","TTID","NewThreadId","OldThreadId",
        "OldThreadState","OldThreadWaitReason","Flag","Flags","StackProcess","StackThread","EventTimeStamp",
        "IoSize","TransferSize","NtStatus"
    };
    static uint U32(byte[] b,int i) { return BitConverter.ToUInt32(b,i); }
    static string InfoString(byte[] b,uint offset) {
        if (offset==0) return "";
        if (offset>=b.Length || (offset&1)!=0) throw new InvalidDataException("Metadata offset");
        int end=(int)offset;
        while(end+1<b.Length && (b[end]!=0 || b[end+1]!=0)) end+=2;
        if(end+1>=b.Length) throw new InvalidDataException("Metadata string");
        return Encoding.Unicode.GetString(b,(int)offset,end-(int)offset);
    }
    static string B64(string s) { return Convert.ToBase64String(Encoding.UTF8.GetBytes(s)); }
    static List<string> Metadata(IntPtr e,string key,StreamWriter output) {
        uint size=0; uint rc=TdhGetEventInformation(e,0,IntPtr.Zero,IntPtr.Zero,ref size);
        if(rc!=122 || size<112 || size>1048576) { output.WriteLine(key+"\terror\t"+rc); return new List<string>(); }
        IntPtr p=Marshal.AllocHGlobal((int)size);
        try {
            uint allocated=size;rc=TdhGetEventInformation(e,0,IntPtr.Zero,p,ref size);
            if(rc!=0 || size>allocated) { output.WriteLine(key+"\terror\t"+rc); return new List<string>(); }
            byte[] b=new byte[size];Marshal.Copy(p,b,0,b.Length);
            uint count=U32(b,100), top=U32(b,104);
            if(top>count || count>512 || 112+count*24>b.Length) throw new InvalidDataException("Metadata count");
            output.WriteLine(key+"\tinfo\t"+U32(b,48)+"\t"+B64(InfoString(b,U32(b,68)))+"\t"+B64(InfoString(b,U32(b,72))));
            var names=new List<string>();
            for(int i=0;i<count;i++) {
                int q=112+i*24;uint flags=U32(b,q);string name=InfoString(b,U32(b,q+4));
                ushort input=BitConverter.ToUInt16(b,q+8),type=BitConverter.ToUInt16(b,q+10),num=BitConverter.ToUInt16(b,q+16);
                output.WriteLine(key+"\tproperty\t"+i+"\t"+B64(name)+"\t"+flags+"\t"+input+"\t"+type+"\t"+num+"\t"+BitConverter.ToUInt16(b,q+18));
                if(i<top && (flags&7)==0 && num==1 && Allowed.Contains(name)) names.Add(name);
            }
            return names;
        } finally { Marshal.FreeHGlobal(p); }
    }
    static string Values(IntPtr e,List<string> names) {
        var values=new List<string>();
        foreach(string name in names) {
            IntPtr p=Marshal.StringToHGlobalUni(name);
            try {
                var descriptor=new Property {Name=(ulong)p.ToInt64(),ArrayIndex=uint.MaxValue};
                uint size;uint rc=TdhGetPropertySize(e,0,IntPtr.Zero,1,ref descriptor,out size);
                if(rc!=0 || (size!=1 && size!=2 && size!=4 && size!=8)) { values.Add(name+"=unavailable:"+rc+":"+size); continue; }
                var b=new byte[size];rc=TdhGetProperty(e,0,IntPtr.Zero,1,ref descriptor,size,b);
                if(rc!=0) { values.Add(name+"=error:"+rc);continue; }
                ulong v=size==1?b[0]:size==2?BitConverter.ToUInt16(b,0):size==4?BitConverter.ToUInt32(b,0):BitConverter.ToUInt64(b,0);
                values.Add(name+"="+v);
            } finally { Marshal.FreeHGlobal(p); }
        }
        return String.Join(";",values);
    }
    // Preserve all generic marker payload DIGESTS, including markers outside the
    // measured-call context. This changes only offline exports, never capture.
    public static bool Select(long time,long lo,long hi,Guid provider,byte opcode) {
        return (lo<=time && time<=hi) ||
            (provider==new Guid("ce1dbfb4-137e-4da6-87b0-3f59aa102cbc") && opcode==34);
    }
    public static void Read(string file,string output,long lo,long hi) {
        if(IntPtr.Size!=8 || !RuntimeInformation.IsOSPlatform(OSPlatform.Windows) || lo<=0 || hi<lo)
            throw new InvalidOperationException("x64 Windows/file QPC interval required");
        if(!Path.IsPathFullyQualified(file) || !File.Exists(file) || new FileInfo(file).Length>268435456 ||
           (File.GetAttributes(file)&FileAttributes.ReparsePoint)!=0 || Directory.Exists(output))
            throw new InvalidOperationException("Ordinary bounded file and new output directory required");
        Directory.CreateDirectory(output);
        var metadata=new Dictionary<string,List<string>>(StringComparer.Ordinal);
        var census=new SortedDictionary<string,long>(StringComparer.Ordinal);
        long callbacks=0,selected=0;string error=null;uint result=uint.MaxValue,close=uint.MaxValue;
        using(var rows=new StreamWriter(Path.Combine(output,"records.tsv"),false,new UTF8Encoding(false)))
        using(var schema=new StreamWriter(Path.Combine(output,"schemas.tsv"),false,new UTF8Encoding(false)))
        using(var sha=SHA256.Create()) {
            rows.WriteLine("qpc\tprovider\tid\topcode\tversion\tpid\ttid\tcpu\tflags\tlength\tpayload_sha256\ttdh_values");
            Callback callback=delegate(IntPtr pointer) {
                if(error!=null)return;
                try {
                    if(++callbacks>2000000)throw new InvalidDataException("Callback limit");
                    Record r=Marshal.PtrToStructure<Record>(pointer);
                    string key=r.Provider+"/"+r.Id+"/"+r.Opcode+"/"+r.Version;
                    census[key]=census.ContainsKey(key)?census[key]+1:1;
                    if(!Select(r.Time,lo,hi,r.Provider,r.Opcode))return;
                    if(++selected>100000)throw new InvalidDataException("Selected record limit");
                    if(!metadata.ContainsKey(key))metadata[key]=Metadata(pointer,key,schema);
                    var b=new byte[r.Length];if(b.Length>0)Marshal.Copy(r.Data,b,0,b.Length);
                    string hash=BitConverter.ToString(sha.ComputeHash(b)).Replace("-","").ToLowerInvariant();
                    rows.WriteLine(r.Time+"\t"+r.Provider+"\t"+r.Id+"\t"+r.Opcode+"\t"+r.Version+"\t"+r.Pid+"\t"+r.Tid+"\t"+r.Cpu+"\t"+r.Flags+"\t"+r.Length+"\t"+hash+"\t"+Values(pointer,metadata[key]));
                } catch(Exception ex) { error=ex.GetType().Name+": "+ex.Message; }
            };
            IntPtr name=Marshal.StringToHGlobalUni(file);ulong handle=ulong.MaxValue;
            try {
                var log=new LogFile {FileName=name,Mode=0x10001000,Callback=Marshal.GetFunctionPointerForDelegate(callback)};
                handle=OpenTraceW(ref log);
                if(handle==ulong.MaxValue)throw new InvalidOperationException("OpenTraceW: "+Marshal.GetLastWin32Error());
                File.WriteAllText(Path.Combine(output,"header.tsv"),"buffer_size\tprocessors\tbuffers_written\tpointer_size\tevents_lost\tfrequency\tclock_type\tbuffers_lost\n"+
                    log.BufferSize+"\t"+log.Processors+"\t"+log.BuffersWritten+"\t"+log.PointerSize+"\t"+log.EventsLost+"\t"+log.Frequency+"\t"+log.ClockType+"\t"+log.BuffersLost+"\n");
                if(log.PointerSize!=8 || log.Frequency!=10000000 || (log.ClockType&3)!=1)throw new InvalidDataException("Native clock/layout");
                result=ProcessTrace(new[]{handle},1,IntPtr.Zero,IntPtr.Zero);
            } finally {
                if(handle!=ulong.MaxValue)close=CloseTrace(handle);
                Marshal.FreeHGlobal(name);GC.KeepAlive(callback);
                using(var f=new StreamWriter(Path.Combine(output,"census.tsv")))foreach(var x in census)f.WriteLine(x.Key+"\t"+x.Value);
                File.WriteAllText(Path.Combine(output,"result.tsv"),"callbacks\tselected\tprocess_trace\tclose_trace\tcallback_error_b64\n"+
                    callbacks+"\t"+selected+"\t"+result+"\t"+close+"\t"+B64(error??"")+"\n");
            }
            if(result!=0 || close!=0 || error!=null)throw new InvalidOperationException("Native decode failed; retain original results");
        }
    }
}}
