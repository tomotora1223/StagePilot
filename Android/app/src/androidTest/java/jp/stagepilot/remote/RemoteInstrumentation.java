package jp.stagepilot.remote;

import android.app.*;
import android.os.*;
import android.content.*;
import android.graphics.Bitmap;
import android.widget.*;
import org.json.*;
import java.io.*;
import java.util.concurrent.atomic.*;
import java.util.function.*;

public final class RemoteInstrumentation extends Instrumentation {
    private final JSONArray results=new JSONArray();
    private int failures=0;
    private MainActivity activity;
    private String host="10.0.2.2",token="stagepilot-test";
    private int port=19780;
    private boolean layoutOnly=false;
    private String scenario="default";
    @Override public void onCreate(Bundle arguments){super.onCreate(arguments);if(arguments!=null){host=arguments.getString("host",host);token=arguments.getString("token",token);port=Integer.parseInt(arguments.getString("port","19780"));layoutOnly="true".equals(arguments.getString("layoutOnly"));scenario=arguments.getString("scenario","default").replaceAll("[^a-zA-Z0-9_-]","");}start();}
    private void check(boolean ok,String title)throws Exception{results.put(new JSONObject().put("test",title).put("passed",ok));if(!ok)++failures;Bundle b=new Bundle();b.putString("stream",(ok?"PASS ":"FAIL ")+title+"\n");sendStatus(0,b);}
    private boolean uiBoolean(BooleanSupplier operation){AtomicBoolean value=new AtomicBoolean();runOnMainSync(()->value.set(operation.getAsBoolean()));return value.get();}
    private JSONObject state(){AtomicReference<JSONObject> value=new AtomicReference<>();runOnMainSync(()->value.set(activity.testState()));return value.get();}
    private boolean waitFor(BooleanSupplier condition,long ms)throws Exception{long until=SystemClock.elapsedRealtime()+ms;do{if(condition.getAsBoolean())return true;Thread.sleep(80);}while(SystemClock.elapsedRealtime()<until);return false;}
    private void screenshot(String name)throws Exception {waitForIdleSync();Thread.sleep(350);waitForIdleSync();Bitmap bitmap=getUiAutomation().takeScreenshot();if(bitmap==null)throw new IOException("Screenshot unavailable");try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getFilesDir(),name+".png"))){bitmap.compress(Bitmap.CompressFormat.PNG,100,out);}bitmap.recycle();}
    private void connectThroughUi(int mode){runOnMainSync(()->activity.testView("connect").performClick());waitForIdleSync();runOnMainSync(()->{((EditText)activity.testDialogView("host")).setText(host);((EditText)activity.testDialogView("port")).setText(""+port);((EditText)activity.testDialogView("token")).setText(token);((Spinner)activity.testDialogView("mode")).setSelection(mode);activity.testDialogSave();});}
    private void configureTranspose(int pad,int value)throws Exception{
        runOnMainSync(()->{activity.testPage(2);activity.testView("edit").performClick();activity.testPad(pad);Spinner actions=(Spinner)activity.testDialogView("pad-action");for(int n=0;n<actions.getCount();++n)if(actions.getItemAtPosition(n).toString().equals("移調を指定して切替")){actions.setSelection(n);break;}});waitForIdleSync();
        runOnMainSync(()->{((EditText)activity.testDialogView("pad-name")).setText("");((Spinner)activity.testDialogView("pad-transpose")).setSelection(value+24);activity.testDialogSave();activity.testView("edit").performClick();});waitForIdleSync();
    }
    private void configureUtility(int pad,String actionName)throws Exception{
        runOnMainSync(()->{activity.testPage(2);activity.testView("edit").performClick();activity.testPad(pad);Spinner actions=(Spinner)activity.testDialogView("pad-action");for(int n=0;n<actions.getCount();++n)if(actions.getItemAtPosition(n).toString().equals(actionName)){actions.setSelection(n);break;}});waitForIdleSync();runOnMainSync(()->{((EditText)activity.testDialogView("pad-name")).setText("");activity.testDialogSave();activity.testView("edit").performClick();});waitForIdleSync();
    }
    private void moveLever(SeekBar slider) {
        long now=android.os.SystemClock.uptimeMillis();
        float x=slider.getWidth()/2f,y=slider.getHeight()/2f;
        for(int event:new int[]{android.view.MotionEvent.ACTION_DOWN,android.view.MotionEvent.ACTION_UP}){
            android.view.MotionEvent motion=android.view.MotionEvent.obtain(now,now,event,x,y,0);
            slider.onTouchEvent(motion);
            motion.recycle();
        }
    }
    private void selectOperation(String tag,String label){
        Spinner field=(Spinner)activity.testDialogView(tag);
        for(int n=0;n<field.getCount();++n)if(label.equals(field.getItemAtPosition(n).toString())){field.setSelection(n);return;}
        throw new IllegalArgumentException(label);
    }
    private void configureGrid(int count,int columns)throws Exception{
        runOnMainSync(()->{activity.testView("edit").performClick();activity.testView("layout").performClick();});waitForIdleSync();runOnMainSync(()->{((Spinner)activity.testDialogView("layout-count")).setSelection(count-1);((Spinner)activity.testDialogView("layout-columns")).setSelection(columns);activity.testDialogSave();activity.testView("edit").performClick();});waitForIdleSync();
    }
    private void migrationCheck()throws Exception{
        JSONArray old=new JSONArray();for(int n=0;n<24;++n)old.put(new JSONObject().put("name","移行"+n).put("action","transpose").put("value",2).put("index",0).put("color","94baff"));getTargetContext().getSharedPreferences("remote",0).edit().clear().putString("pads",old.toString()).commit();Intent intent=new Intent(getTargetContext(),MainActivity.class);intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);activity=(MainActivity)startActivitySync(intent);waitForIdleSync();
        check(uiBoolean(()->((TextView)activity.testView("pad0-caption")).getText().toString().equals("移行0")),"Legacy first page keeps original slot");runOnMainSync(()->activity.testPage(1));check(uiBoolean(()->((TextView)activity.testView("pad0-caption")).getText().toString().equals("移行8")),"Legacy second page keeps original slot");runOnMainSync(()->{activity.testPage(2);activity.testView("edit").performClick();activity.testPad(5);});waitForIdleSync();check(uiBoolean(()->((Spinner)activity.testDialogView("pad-transpose")).getSelectedItemPosition()==26),"Legacy third page retains absolute transpose value");runOnMainSync(()->activity.testDialogSave());check(uiBoolean(()->{try{JSONArray saved=new JSONArray(activity.testLayout());return saved.length()==96&&saved.getJSONObject(69).getString("name").equals("移行21")&&saved.getJSONObject(71).getString("color").equals("94baff");}catch(Exception ex){return false;}}),"Migrated 96-slot storage retains last-page names and colours");connectThroughUi(0);check(waitFor(()->uiBoolean(activity::testReady),5000)&&uiBoolean(()->activity.testLayout().contains("移行21")),"First connection does not overwrite an existing manually saved layout");runOnMainSync(()->activity.finish());waitForIdleSync();
    }
    @Override public void onStart(){
        if(layoutOnly){try{layoutChecks();}catch(Exception ex){try{check(false,"Layout check error: "+ex);}catch(Exception ignored){}}writeReport();return;}
        try{
            interruptedReplyCheck();migrationCheck();
            getTargetContext().getSharedPreferences("remote",0).edit().clear().commit();Intent intent=new Intent(getTargetContext(),MainActivity.class);intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);activity=(MainActivity)startActivitySync(intent);waitForIdleSync();check(uiBoolean(()->!activity.testReady()),"Launch disconnected and do not send commands");
            connectThroughUi(0);check(waitFor(()->uiBoolean(activity::testReady),7000),"Connection dialog authenticates with actual PC HTTP server");check(state().optJSONArray("items").length()==5,"PC catalog includes Japanese scenes, instruments and effects");check(uiBoolean(()->{try{JSONArray values=new JSONArray(activity.testLayout());return values.getJSONObject(0).optString("action").equals("instrument")&&values.getJSONObject(1).optString("action").equals("instrument")&&values.getJSONObject(2).optString("action").equals("scene")&&values.getJSONObject(3).optString("action").equals("scene");}catch(Exception ex){return false;}}),"Fresh performance page puts instruments and combined sounds together");
            runOnMainSync(()->{activity.testView("edit").performClick();activity.testPad(2);((EditText)activity.testDialogView("pad-name")).setText("鳥の歌・ピアノ");((Spinner)activity.testDialogView("pad-color")).setSelection(2);});screenshot("Android-ボタン編集");runOnMainSync(()->{activity.testDialogSave();activity.testView("edit").performClick();});check(uiBoolean(()->activity.testLayout().contains("鳥の歌・ピアノ")&&activity.testLayout().contains("94baff")),"Button name and colour are saved through edit UI");
            runOnMainSync(()->activity.testPad(2));check(waitFor(()->state().optInt("scene",-1)==0,3000),"Named on-screen button selects layered scene on PC");screenshot("Android-音色");
            configureUtility(3,"移調を＋1半音");configureUtility(4,"移調を−1半音");
            configureTranspose(5,2);configureTranspose(6,-3);configureTranspose(7,0);
            check(uiBoolean(()->activity.testLayout().contains("移調 +2 半音")&&activity.testLayout().contains("移調 -3 半音")),"Transpose preset values and automatic Japanese captions saved through UI");
            runOnMainSync(()->{activity.testView("edit").performClick();activity.testPad(5);});waitForIdleSync();check(uiBoolean(()->((Spinner)activity.testDialogView("pad-transpose")).getSelectedItemPosition()==26),"Reopening editor retains +2 preset");screenshot("Android-移調ボタン設定");runOnMainSync(()->{activity.testDialogSave();activity.testView("edit").performClick();});
            for(int mode=0;mode<3;++mode){connectThroughUi(mode);check(waitFor(()->uiBoolean(activity::testReady),4000),"Mode "+mode+": authenticated state sync");runOnMainSync(()->{activity.testPage(0);activity.testPad(3);});check(waitFor(()->state().optInt("scene",-1)==1,3000),"Mode "+mode+": touchscreen selects split scene");runOnMainSync(()->{activity.testPage(0);activity.testPad(0);});check(waitFor(()->state().optInt("scene",0)==-1&&state().optInt("instrument",-1)==0,3000),"Mode "+mode+": touchscreen selects standalone instrument");
                runOnMainSync(()->{activity.testPage(2);activity.testPad(0);});check(waitFor(()->state().optInt("scene",0)==-1&&state().optInt("instrument",-1)==1,2500),"Mode "+mode+": next button advances to next source");Thread.sleep(120);runOnMainSync(()->activity.testPad(0));check(waitFor(()->state().optInt("scene",-1)==0,2500),"Mode "+mode+": next button crosses into combined sound without changing page");Thread.sleep(120);runOnMainSync(()->activity.testPad(1));check(waitFor(()->state().optInt("scene",0)==-1&&state().optInt("instrument",-1)==1,2500),"Mode "+mode+": previous button crosses back into source");
                RemoteClient testClient=new RemoteClient(new RemoteClient.Config(host,port,token,mode));testClient.state();testClient.command("effect",0,1);check(waitFor(()->testClientStatePlaying(testClient),800),"Mode "+mode+": actual effect playback flag observed without audio output");testClient.command("effectsStop",0,1);check(waitFor(()->!testClientStatePlaying(testClient),1000),"Mode "+mode+": effect stop applied");
                runOnMainSync(()->{activity.testPage(1);activity.testPad(0);});check(waitFor(()->state().optJSONArray("playing").length()>0,1200),"Mode "+mode+": effect button goes through production command queue");if(mode==2)screenshot("Android-効果音");testClient.command("master",0,.43f);testClient.command("transpose",0,7);check(waitFor(()->Math.abs(state().optDouble("master")-.43)<.01&&state().optInt("transpose")==7,2000),"Mode "+mode+": master volume and transpose synchronize");
                runOnMainSync(()->activity.testView("reset-volume").performClick());check(waitFor(()->Math.abs(state().optDouble("master")-.7)<.01,2000),"Mode "+mode+": output reset button applies 70 percent");runOnMainSync(()->{activity.testPage(2);activity.testPad(5);});check(waitFor(()->state().optInt("transpose")==2,2500),"Mode "+mode+": one-touch +2 replaces previous +7");Thread.sleep(150);runOnMainSync(()->activity.testPad(5));Thread.sleep(600);check(state().optInt("transpose")==2,"Mode "+mode+": repeated preset does not accumulate");check(uiBoolean(()->((LinearLayout)activity.testView("pad5")).getContentDescription().toString().contains("適用中")),"Mode "+mode+": active preset highlighted from PC state");
                runOnMainSync(()->activity.testPad(6));check(waitFor(()->state().optInt("transpose")==-3,2500),"Mode "+mode+": one-touch negative preset replaces +2");if(mode==2)screenshot("Android-移調ワンタッチ");
                runOnMainSync(()->activity.testPad(7));check(waitFor(()->state().optInt("transpose")==0,2500),"Mode "+mode+": zero preset restores original key");check(uiBoolean(()->((TextView)activity.testView("active-shift")).getText().toString().contains("0 半音")&&activity.testView("active-shift").isShown()),"Mode "+mode+": zero transpose remains visible");
                runOnMainSync(()->activity.testPad(3));check(waitFor(()->state().optInt("transpose")==1,2500),"Mode "+mode+": +1 button increments current transpose");Thread.sleep(150);runOnMainSync(()->activity.testPad(3));check(waitFor(()->state().optInt("transpose")==2,2500),"Mode "+mode+": repeated +1 accumulates");runOnMainSync(()->activity.testPad(4));check(waitFor(()->state().optInt("transpose")==1,2500),"Mode "+mode+": -1 button decrements current transpose");
                testClient.state();testClient.command("panic",0,1);testClient.command("transpose",0,0);
            }
            runOnMainSync(()->activity.finish());waitForIdleSync();activity=(MainActivity)startActivitySync(intent);check(waitFor(()->uiBoolean(activity::testReady),5000),"Activity relaunch reconnects without replaying previous buttons");check(uiBoolean(()->activity.testLayout().contains("鳥の歌・ピアノ")),"Custom button name persists across activity relaunch");
            runOnMainSync(()->{activity.testPage(2);activity.testPad(6);});check(waitFor(()->state().optInt("transpose")==-3,2500),"Saved -3 preset survives activity relaunch");
            runOnMainSync(()->activity.activate(new RemoteClient.Config(host,port,"wrong-pairing-key",0)));check(waitFor(()->uiBoolean(()->!activity.testReady()&&((TextView)activity.testView("status")).getText().toString().contains("接続キー")),4000),"Wrong pairing key shows error and disables performance buttons");check(uiBoolean(()->!activity.testView("pad0").isEnabled()),"Disconnected assigned button cannot enqueue a sound command");screenshot("Android-接続エラー");
            connectThroughUi(0);check(waitFor(()->uiBoolean(activity::testReady),4000),"Recovery reconnects and enables buttons");runOnMainSync(()->{activity.testPage(0);activity.testPad(2);});check(waitFor(()->state().optInt("scene",-1)==0,3000),"Named scene button works after reconnect");screenshot("Android-演奏");
            check(uiBoolean(activity::testTextFits),"Portrait names, status, page buttons and volume fit measured text layouts");ActivityMonitor rotation=addMonitor(MainActivity.class.getName(),null,false);runOnMainSync(()->activity.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));Activity rotated=rotation.waitForActivityWithTimeout(4000);if(rotated instanceof MainActivity)activity=(MainActivity)rotated;removeMonitor(rotation);check(waitFor(()->uiBoolean(activity::testReady),4000)&&uiBoolean(()->activity.testColumns()==4),"Landscape recreation preserves connection and uses four pad columns");check(uiBoolean(activity::testAllPadsVisible),"All eight landscape buttons fit without scrolling");check(uiBoolean(activity::testTextFits),"Landscape names and status retain measured line height");screenshot("Android-横画面");
            ActivityMonitor portrait=addMonitor(MainActivity.class.getName(),null,false);runOnMainSync(()->activity.setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));Activity back=portrait.waitForActivityWithTimeout(4000);if(back instanceof MainActivity)activity=(MainActivity)back;removeMonitor(portrait);check(waitFor(()->uiBoolean(activity::testReady),4000)&&uiBoolean(()->activity.testColumns()==2),"Portrait recreation preserves named layout and restores two columns");screenshot("Android-演奏");
            runOnMainSync(()->activity.testPage(2));configureGrid(12,3);check(uiBoolean(()->activity.testColumns()==3&&activity.testView("pad11")!=null&&activity.testView("pad12")==null),"Page grid changes to 12 buttons in 3 columns");screenshot("Android-12ボタン3列");configureGrid(32,4);configureUtility(31,"移調を＋1半音");runOnMainSync(()->{ScrollView list=(ScrollView)activity.testView("pad-scroll");list.scrollTo(0,list.getChildAt(0).getHeight());});check(waitFor(()->uiBoolean(()->activity.testView("pad31").getGlobalVisibleRect(new android.graphics.Rect())&&activity.testView("volume").isShown()),1500),"32nd button reachable with persistent master volume");screenshot("Android-32ボタン4列");
            configureGrid(8,2);check(uiBoolean(()->activity.testView("pad31")==null),"Reducing button count hides extra buttons");configureGrid(32,4);check(uiBoolean(()->((TextView)activity.testView("pad31-caption")).getText().toString().equals("移調を＋1半音")),"Restoring button count retains hidden assignment");runOnMainSync(()->activity.testPage(0));check(uiBoolean(()->activity.testColumns()==2&&activity.testView("pad8")==null),"Grid preferences are independent per page");runOnMainSync(()->activity.finish());waitForIdleSync();activity=(MainActivity)startActivitySync(intent);check(waitFor(()->uiBoolean(activity::testReady),5000),"Reconnect after saving expanded grid");runOnMainSync(()->activity.testPage(2));check(uiBoolean(()->activity.testColumns()==4&&activity.testView("pad31")!=null),"32-button 4-column grid persists after relaunch");runOnMainSync(()->activity.testPad(31));check(waitFor(()->state().optInt("transpose")== -2,2500),"Saved 32nd button sends increment after relaunch");
            runOnMainSync(()->{activity.testView("edit").performClick();activity.testPad(8);selectOperation("pad-action","移調を指定して切替");});
            waitForIdleSync();
            runOnMainSync(()->{((Spinner)activity.testDialogView("pad-transpose")).setSelection(29);activity.testDialogView("add-operation").performClick();selectOperation("extra-action-1","全体音量を指定");});
            waitForIdleSync();
            runOnMainSync(()->{((Spinner)activity.testDialogView("extra-value-1")).setSelection(39);activity.testDialogSave();activity.testView("edit").performClick();activity.testPad(8);});
            check(waitFor(()->state().optInt("transpose")==5&&Math.abs(state().optDouble("master")-.39)<.005,3000),"One edited Android pad applies both pitch transpose and volume");
            configureUtility(9,"ピッチベンドのレバー");
            runOnMainSync(()->{activity.testPage(2);((ScrollView)activity.testView("pad-scroll")).fullScroll(android.view.View.FOCUS_UP);});
            waitForIdleSync();
            check(uiBoolean(()->activity.testView("pad9-lever").isShown()&&activity.testView("pad9-lever").isEnabled()),"Pitch lever is displayed and enabled in its chosen grid slot");
            screenshot("Android-複数操作とピッチベンド");
            configureUtility(10,"モジュレーションのレバー");
            runOnMainSync(()->activity.testPage(2));
            Thread.sleep(300);
            runOnMainSync(()->moveLever((SeekBar)activity.testView("pad10-lever")));
            check(waitFor(()->state().optInt("modulation")>0,3000),"Dragging an Android modulation lever sends and holds CC1");
            configureUtility(11,"エクスプレッションのレバー");
            runOnMainSync(()->activity.testPage(2));
            Thread.sleep(300);
            runOnMainSync(()->moveLever((SeekBar)activity.testView("pad11-lever")));
            check(waitFor(()->state().optInt("expression",127)<100,3000)&&state().optInt("modulation")>0,"Expression and modulation have independent positions");
            check(MainActivity.parsePairing("https://10.0.2.2:19780/stagepilot-test",0)==null&&
                  MainActivity.parsePairing("stagepilot://10.0.2.2:19780/short",0)==null,"Reject non-StagePilot QR payload and short keys");
            runOnMainSync(()->{
                Intent result=new Intent().putExtra("SCAN_RESULT","stagepilot://"+host+":"+port+"/"+token).putExtra("SCAN_RESULT_FORMAT","QR_CODE");
                activity.onActivityResult(com.google.zxing.integration.android.IntentIntegrator.REQUEST_CODE,Activity.RESULT_OK,result);
            });
            check(waitFor(()->uiBoolean(activity::testReady),4000),"Scanner result opens an authenticated connection without typing an IP, port or key");


        }catch(Exception ex){try{check(false,"Unexpected error: "+ex);StringWriter trace=new StringWriter();ex.printStackTrace(new PrintWriter(trace));Bundle detail=new Bundle();detail.putString("stream",trace.toString());sendStatus(0,detail);}catch(Exception ignored){}}
        writeReport();
    }
    private void layoutChecks()throws Exception{
        Intent intent=new Intent(getTargetContext(),MainActivity.class);intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TASK);activity=(MainActivity)startActivitySync(intent);waitForIdleSync();boolean landscape=scenario.startsWith("landscape");int direction=landscape?android.content.res.Configuration.ORIENTATION_LANDSCAPE:android.content.res.Configuration.ORIENTATION_PORTRAIT;
        if(getTargetContext().getResources().getConfiguration().orientation!=direction){ActivityMonitor rotation=addMonitor(MainActivity.class.getName(),null,false);runOnMainSync(()->activity.setRequestedOrientation(landscape?android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE:android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));Activity rotated=rotation.waitForActivityWithTimeout(4000);if(rotated instanceof MainActivity)activity=(MainActivity)rotated;removeMonitor(rotation);waitForIdleSync();}
        check(uiBoolean(()->activity.testColumns()==(landscape?4:2)),"Requested scenario orientation uses expected pad columns");check(waitFor(()->uiBoolean(activity::testReady),5000),"Reconnect with retained layout without replaying commands");
        runOnMainSync(()->{activity.testPage(0);activity.testView("edit").performClick();activity.testPad(0);});waitForIdleSync();runOnMainSync(()->((EditText)activity.testDialogView("pad-name")).setText("鳥の歌・ピアノとパッドを重ねたライブ演奏用の音色"));screenshot("Android-長い名前の編集-"+scenario);runOnMainSync(()->{activity.testDialogSave();activity.testView("edit").performClick();});waitForIdleSync();check(uiBoolean(activity::testTextFits),"Long Japanese pad name wraps completely without shrinking or clipping at configured font scale");screenshot("Android-長い名前-"+scenario);
        runOnMainSync(()->((ScrollView)activity.testView("pad-scroll")).fullScroll(android.view.View.FOCUS_DOWN));waitForIdleSync();check(waitFor(()->uiBoolean(()->activity.testView("pad7").getGlobalVisibleRect(new android.graphics.Rect())),1500),"Last button remains reachable through scrolling");check(uiBoolean(()->activity.testView("volume").isShown()&&activity.testView("reset-volume").isShown()),"Master volume remains outside the scrolling button area");screenshot("Android-スクロール下部-"+scenario);
        runOnMainSync(()->((ScrollView)activity.testView("pad-scroll")).fullScroll(android.view.View.FOCUS_UP));runOnMainSync(()->{activity.testView("edit").performClick();activity.testPad(0);});waitForIdleSync();runOnMainSync(()->{((EditText)activity.testDialogView("pad-name")).setText("鳥の歌・ピアノ");activity.testDialogSave();activity.testView("edit").performClick();});waitForIdleSync();check(uiBoolean(activity::testTextFits),"Original name restores with no truncated glyph lines");screenshot("Android-文字サイズ-"+scenario);
    }
    private void interruptedReplyCheck()throws Exception{
        try(java.net.ServerSocket server=new java.net.ServerSocket(0,4,java.net.InetAddress.getByName("127.0.0.1"))){
            server.setSoTimeout(2000);AtomicBoolean sameId=new AtomicBoolean(false);AtomicReference<Throwable> error=new AtomicReference<>();
            Thread peer=new Thread(()->{try{String original="";for(int round=0;round<3;++round){try(java.net.Socket socket=server.accept()){
                socket.setSoTimeout(1500);InputStream in=socket.getInputStream();ByteArrayOutputStream head=new ByteArrayOutputStream();int marker=0;while(marker<4){int b=in.read();if(b<0)throw new IOException("request header incomplete");head.write(b);int expected=marker%2==0?'\r':'\n';marker=b==expected?marker+1:b=='\r'?1:0;}
                int length=0;for(String line:head.toString("US-ASCII").split("\r\n"))if(line.startsWith("Content-Length:"))length=Integer.parseInt(line.substring(15).trim());byte[] payload=new byte[length];int offset=0;while(offset<length){int n=in.read(payload,offset,length-offset);if(n<0)throw new IOException("request body incomplete");offset+=n;}JSONObject request=new JSONObject(new String(payload,java.nio.charset.StandardCharsets.UTF_8));
                JSONObject reply=new JSONObject().put("ok",true);if(round==0)reply.put("nowMs",System.currentTimeMillis());else{String id=request.getString("id");if(round==1)original=id;else sameId.set(original.equals(id));reply.put("id",id).put("duplicate",round==2);}
                byte[] bytes=reply.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8);OutputStream out=socket.getOutputStream();out.write(("HTTP/1.1 200 OK\r\nContent-Length: "+bytes.length+"\r\nConnection: close\r\n\r\n").getBytes(java.nio.charset.StandardCharsets.US_ASCII));out.write(bytes,0,round==1?bytes.length-1:bytes.length);out.flush();
            }}}catch(Throwable ex){error.set(ex);}},"Interrupted-reply-test");peer.start();
            RemoteClient test=new RemoteClient(new RemoteClient.Config("127.0.0.1",server.getLocalPort(),"stagepilot-test",0));test.state();JSONObject reply=test.command("effect",0,1);peer.join(2500);check(!peer.isAlive()&&error.get()==null&&sameId.get()&&reply.optBoolean("duplicate"),"Truncated HTTP reply retries the same operation ID and accepts duplicate acknowledgement");
        }
    }
    private void writeReport(){
        try{JSONObject report=new JSONObject().put("failures",failures).put("results",results).put("physicalAudioOutput",false).put("device",Build.MODEL).put("fontScale",getTargetContext().getResources().getConfiguration().fontScale);try(FileOutputStream out=new FileOutputStream(new File(getTargetContext().getFilesDir(),layoutOnly?"android-layout-"+scenario+".json":"android-tests.json"))){out.write(report.toString(2).getBytes(java.nio.charset.StandardCharsets.UTF_8));}}catch(Exception ex){++failures;}
        Bundle summary=new Bundle();summary.putString("stream","\nTests: "+results.length()+"  Failures: "+failures+"\n");finish(failures==0?Activity.RESULT_OK:Activity.RESULT_CANCELED,summary);
    }
    private boolean testClientStatePlaying(RemoteClient c){try{return c.state().optJSONArray("playing").length()>0;}catch(Exception ex){return false;}}
}
