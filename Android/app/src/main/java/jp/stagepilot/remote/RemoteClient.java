package jp.stagepilot.remote;

import org.json.*;
import java.net.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.UUID;
import android.os.SystemClock;

public final class RemoteClient {
    public static final class Config {
        public final String host,token; public final int port,mode;
        public Config(String h,int p,String t,int m){host=h;port=p;token=t;mode=m;}
        public boolean valid(){return !host.trim().isEmpty()&&host.length()<254&&!host.contains("/")&&!host.contains(":")&&port>=1024&&port<=65533&&token.length()>=8&&mode>=0&&mode<=2;}
    }
    private final Config config;
    private volatile long serverTime=0,observedAt=0;
    public RemoteClient(Config c){config=c;}
    public Config config(){return config;}
    public JSONObject state() throws Exception {
        JSONObject q=new JSONObject().put("op","state").put("token",config.token);JSONObject s=http(q);
        requireOk(s);serverTime=s.getLong("nowMs");observedAt=SystemClock.elapsedRealtime();return s;
    }
    public JSONObject command(String action,int index,float value) throws Exception {
        long observed=observedAt,now=SystemClock.elapsedRealtime();if(observed==0||now-observed>2500)throw new IOException("切断中です。接続を確認してください");
        JSONObject q=new JSONObject().put("op","command").put("token",config.token).put("id",UUID.randomUUID().toString()).put("action",action).put("index",index).put("value",value).put("expires",serverTime+(now-observed)+1500);
        JSONObject response=exchange(q);requireOk(response);if(!response.optString("id").equals(q.getString("id")))throw new IOException("操作IDが違います");return response;
    }
    private JSONObject exchange(JSONObject request)throws Exception{
        if(config.mode==2)return osc(request);
        long started=SystemClock.elapsedRealtime();
        for(int attempt=0;attempt<2;++attempt){try{return config.mode==0?http(request):tcp(request);}catch(IOException|JSONException ex){
            // Preserve both ID and deadline. If the first response is interrupted,
            // the PC can acknowledge the original command without sounding it twice.
            if(attempt==1||SystemClock.elapsedRealtime()-started>700)throw ex;
        }}throw new IOException("操作の結果を確認できません");
    }
    private void requireOk(JSONObject response) throws Exception {
        if(response.optBoolean("ok"))return;String code=response.optString("error");String message=switch(code){case "auth" -> "接続キーが違います";case "busy" -> "PCが音源を読み込み中です";case "unavailable" -> "この音色・効果音はPCのセットにありません";case "expired" -> "操作の期限が切れました。もう一度押してください";case "rate" -> "操作が集中しています。少し待ってください";default -> "PCが操作を受け付けませんでした："+code;};throw new IOException(message);
    }
    private JSONObject http(JSONObject q) throws Exception {
        // Small HTTP/1.1 client with an explicit connection lifetime; no shared connection pool.
        try(Socket socket=new Socket()){
            socket.connect(new InetSocketAddress(config.host,config.port),800);socket.setSoTimeout(900);socket.setTcpNoDelay(true);byte[] data=q.toString().getBytes(StandardCharsets.UTF_8);
            String header="POST /api HTTP/1.1\r\nHost: "+config.host+":"+config.port+"\r\nConnection: close\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: "+data.length+"\r\n\r\n";
            OutputStream output=socket.getOutputStream();output.write(header.getBytes(StandardCharsets.US_ASCII));output.write(data);output.flush();InputStream input=socket.getInputStream();ByteArrayOutputStream headers=new ByteArrayOutputStream();int marker=0;
            while(marker<4){int b=input.read();if(b<0||headers.size()>8192)throw new IOException("PCの応答ヘッダーが不正です");headers.write(b);int expected=(marker%2==0)?'\r':'\n';marker=b==expected?marker+1:b=='\r'?1:0;}
            String h=new String(headers.toByteArray(),StandardCharsets.US_ASCII);if(!h.startsWith("HTTP/1.1 200 ")&&!h.startsWith("HTTP/1.1 400 "))throw new IOException("PCの応答コードが不正です");int length=-1;for(String line:h.split("\r\n"))if(line.toLowerCase(java.util.Locale.ROOT).startsWith("content-length:"))length=Integer.parseInt(line.substring(line.indexOf(':')+1).trim());if(length<0||length>65536)throw new IOException("PCの応答サイズが不正です");
            byte[] body=new byte[length];int offset=0;while(offset<length){int n=input.read(body,offset,length-offset);if(n<0)throw new IOException("PCの応答が途中で途切れました ("+offset+"/"+length+")");offset+=n;}return new JSONObject(new String(body,StandardCharsets.UTF_8));
        }
    }
    private JSONObject tcp(JSONObject q) throws Exception {
        try(Socket socket=new Socket()){socket.connect(new InetSocketAddress(config.host,config.port+1),800);socket.setSoTimeout(900);socket.setTcpNoDelay(true);socket.getOutputStream().write((q+"\n").getBytes(StandardCharsets.UTF_8));ByteArrayOutputStream data=new ByteArrayOutputStream();InputStream in=socket.getInputStream();int b;while((b=in.read())!=-1&&b!='\n'){if(data.size()>=65536)throw new IOException("応答が大きすぎます");data.write(b);}if(data.size()==0)throw new IOException("PCから応答がありません");return new JSONObject(new String(data.toByteArray(),StandardCharsets.UTF_8));}
    }
    private static void oscString(DataOutputStream out,String s) throws IOException {byte[] data=s.getBytes(StandardCharsets.UTF_8);out.write(data);out.writeByte(0);for(int i=data.length+1;i%4!=0;++i)out.writeByte(0);}
    private JSONObject osc(JSONObject q) throws Exception {
        ByteArrayOutputStream bytes=new ByteArrayOutputStream();DataOutputStream out=new DataOutputStream(bytes);oscString(out,"/stagepilot/command");oscString(out,",sssifs");oscString(out,config.token);oscString(out,q.getString("id"));oscString(out,q.getString("action"));out.writeInt(q.getInt("index"));out.writeFloat((float)q.getDouble("value"));oscString(out,q.getString("expires"));byte[] request=bytes.toByteArray();
        try(DatagramSocket socket=new DatagramSocket()){socket.connect(InetAddress.getByName(config.host),config.port+2);socket.setSoTimeout(350);
            // One retry uses the identical ID. PC de-duplicates it before creating an audio event.
            for(int attempt=0;attempt<2;++attempt){socket.send(new DatagramPacket(request,request.length));byte[] reply=new byte[8192];DatagramPacket response=new DatagramPacket(reply,reply.length);try{socket.receive(response);int at=0;String address=oscReadString(reply,response.getLength(),at);at=nextOsc(reply,response.getLength(),at);String tags=oscReadString(reply,response.getLength(),at);at=nextOsc(reply,response.getLength(),at);if(!address.equals("/stagepilot/reply")||!tags.equals(",s"))throw new IOException("OSC応答が不正です");JSONObject json=new JSONObject(oscReadString(reply,response.getLength(),at));if(json.optBoolean("ok")&&!json.optString("id").equals(q.getString("id")))throw new IOException("操作IDが違います");return json;}catch(SocketTimeoutException ex){if(attempt==1)throw new IOException("操作の結果を確認できません。PCの状態を確認してください",ex);}}
        }throw new IOException("OSC応答がありません");
    }
    private static int nextOsc(byte[] data,int length,int at) throws IOException {while(at<length&&data[at]!=0)++at;if(at>=length)throw new IOException("OSC応答が不正です");int end=(at+4)&~3;if(end>length)throw new IOException("OSC応答が不正です");for(int p=at;p<end;++p)if(data[p]!=0)throw new IOException("OSC応答が不正です");return end;}
    private static String oscReadString(byte[] data,int length,int at)throws IOException {int end=at;while(end<length&&data[end]!=0)++end;if(end>=length)throw new IOException("OSC応答が不正です");return new String(data,at,end-at,StandardCharsets.UTF_8);}
}
