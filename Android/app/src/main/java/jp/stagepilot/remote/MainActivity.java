package jp.stagepilot.remote;

import android.app.*;
import android.os.*;
import android.provider.Settings;
import android.net.Uri;
import com.google.zxing.integration.android.IntentIntegrator;
import com.google.zxing.integration.android.IntentResult;
import android.content.*;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.content.res.ColorStateList;
import android.view.*;
import android.widget.*;
import org.json.*;
import java.util.*;
import java.util.concurrent.*;

public final class MainActivity extends Activity {
    private static final int BG=0xff101419, PANEL=0xff1b222b,INK=0xffeef3f6,MUTED=0xffa6b5c6,MINT=0xff72d6c2;
    private static final String[] COLOURS={"72d6c2","ffbb70","94baff","eaa0db","c3d77a","bdabff","ff9299","7dd7ef"};
    private static final String[] COLOUR_NAMES={"ミント","オレンジ","青","ピンク","黄緑","紫","赤","水色"};
    private static final String[] UTILITY_ACTIONS={"next","previous","fade","restore","panic","effectsStop","sequence","sequenceStop","transpose","transposeUp","transposeDown","master","masterStep","pitch","modulation","expression"};
    private static final String[] UTILITY_NAMES={"次の音色","前の音色","ゆっくり消音","音量を戻す","すべての音を止める","効果音をすべて停止","打ち込みを再生","打ち込みを停止","移調を指定して切替","移調を＋1半音","移調を−1半音","全体音量を指定","全体音量を増減","ピッチベンドのレバー","モジュレーションのレバー","エクスプレッションのレバー"};
    private final Handler main=new Handler(Looper.getMainLooper());
    private final ScheduledExecutorService poller=Executors.newSingleThreadScheduledExecutor();
    private final ThreadPoolExecutor commands=new ThreadPoolExecutor(1,1,0,TimeUnit.SECONDS,new ArrayBlockingQueue<>(8));
    private android.content.SharedPreferences prefs;
    private volatile RemoteClient client;
    private volatile boolean foreground=false,ready=false;
    private JSONObject snapshot=new JSONObject();
    private String cachedCatalog="";
    private static final int MAX_PADS=32;
    private final JSONObject[] pads=new JSONObject[MAX_PADS*3];
    private final int[] padCounts={8,8,8},columnCounts={0,0,0};
    private final String[] pageNames={"演奏","効果音","フリー"};
    private int page=0,failures=0;
    private boolean editing=false,volumeTracking=false,updatingVolume=false;
    private boolean compact=false;
    private LinearLayout root;
    private GridLayout grid;
    private TextView status,active,feedback,volumeLabel;
    private TextView activeShift;
    private Typeface regular,bold;
    private Button edit,layoutButton;
    private SeekBar volume;
    private final Button[] pageButtons=new Button[3];
    private final PadButton[] padButtons=new PadButton[MAX_PADS];
    private final class PadButton extends LinearLayout {
        final TextView caption,source,state;
        final View accentBar;
        final SeekBar lever;
        String displayed="";
        PadButton(String tag){super(MainActivity.this);setTag(tag);setOrientation(HORIZONTAL);setGravity(Gravity.CENTER_VERTICAL);setPadding(dp(10),dp(compact?5:14),dp(12),dp(compact?5:14));setMinimumHeight(dp(compact?76:128));setSoundEffectsEnabled(false);setClickable(true);setFocusable(true);
            accentBar=new View(MainActivity.this);LinearLayout.LayoutParams stripe=new LinearLayout.LayoutParams(dp(3),-1);stripe.setMargins(0,dp(2),dp(10),dp(2));addView(accentBar,stripe);
            LinearLayout words=new LinearLayout(MainActivity.this);words.setOrientation(VERTICAL);words.setGravity(Gravity.CENTER_VERTICAL);addView(words,new LinearLayout.LayoutParams(0,-2,1));
            caption=text("",(compact||grid.getColumnCount()>=3)?16:21,INK);caption.setTypeface(bold);caption.setTag(tag+"-caption");
            source=text("",compact?11:12,MUTED);source.setMaxLines(2);source.setEllipsize(android.text.TextUtils.TruncateAt.END);source.setTag(tag+"-source");
            state=text("",compact?11:12,MUTED);state.setMaxLines(2);state.setTag(tag+"-state");
            words.addView(caption,new LinearLayout.LayoutParams(-1,-2));LinearLayout.LayoutParams detail=new LinearLayout.LayoutParams(-1,-2);detail.topMargin=dp(compact?0:4);words.addView(source,detail);LinearLayout.LayoutParams message=new LinearLayout.LayoutParams(-1,-2);message.topMargin=dp(compact?0:5);words.addView(state,message);
            lever=new SeekBar(MainActivity.this);
            lever.setMax(16383);lever.setProgress(8192);
            lever.setContentDescription("ピッチベンド");lever.setTag(tag+"-lever");
            lever.setVisibility(GONE);words.addView(lever,new LinearLayout.LayoutParams(-1,dp(38)));
            lever.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
                public void onProgressChanged(SeekBar bar,int value,boolean user){if(user&&activeLever==bar)leverValue=value;}
                public void onStartTrackingTouch(SeekBar bar){
                    endLever();
                    bar.getParent().requestDisallowInterceptTouchEvent(true);
                    activeLever=bar;leverAction=(String)bar.getTag(R.id.lever_action);leverValue=bar.getProgress();main.post(leverTick);
                }
                public void onStopTrackingTouch(SeekBar bar){if(activeLever==bar)endLever();}
            });
            for(TextView t:new TextView[]{caption,source,state})t.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        }
        void display(String name,String description,String message,int accent,boolean selected,boolean empty){
            String next=name+"\n"+description+"\n"+message+accent+selected+empty;if(next.equals(displayed))return;displayed=next;
            caption.setText(name);caption.setTextSize(empty?16:(compact||grid.getColumnCount()>=3)?16:21);caption.setTypeface(empty?regular:bold);source.setText(description);source.setVisibility(description.isEmpty()?GONE:VISIBLE);state.setText(message);caption.setTextColor(empty?MUTED:INK);state.setTextColor(selected?accent:MUTED);accentBar.setBackgroundColor(accent);accentBar.setVisibility(empty?INVISIBLE:VISIBLE);
            setBackground(new RippleDrawable(ColorStateList.valueOf((accent&0xffffff)|0x33000000),shape(selected?blend(PANEL,accent,.16f):empty?0xff151c24:PANEL,selected?accent:empty?0xff27323d:0xff344150,1),null));
            setContentDescription(name+"、"+description+"、"+message);
        }
    }
    private SeekBar activeLever;
    private String leverAction="pitch";
    private int leverValue;
    private boolean leverInFlight;
    private long wheelPendingUntil;
    private final Runnable leverTick=new Runnable(){
        public void run(){
            if(activeLever==null||!foreground||!ready){endLever();return;}
            sendLever(leverAction,leverValue,false);
            main.postDelayed(this,50);
        }
    };
    private static boolean isWheel(String action){return action.equals("modulation")||action.equals("expression");}
    private static boolean isLever(String action){return action.equals("pitch")||isWheel(action);}
    private void endLever(){
        main.removeCallbacks(leverTick);
        if(activeLever==null)return;
        if(leverAction.equals("pitch")){
            activeLever.setProgress(8192);
            leverValue=8192;
        }else{
            wheelPendingUntil=SystemClock.elapsedRealtime()+700;
        }
        activeLever.getParent().requestDisallowInterceptTouchEvent(false);
        activeLever=null;
        sendLever(leverAction,leverValue,true);
    }
    private void sendLever(String action,int value,boolean finalValue){
        RemoteClient target=client;
        if(target==null||(!ready&&!finalValue)||(leverInFlight&&!finalValue))return;
        leverInFlight=true;
        long pressed=SystemClock.elapsedRealtime();
        try{
            commands.execute(()->{
                try{if(client==target&&(finalValue||foreground)&&SystemClock.elapsedRealtime()-pressed<400)
                    target.command(action,0,value);
                }catch(Exception ignored){
                    // Pitch returns to centre on the PC if its stream stops.
                }finally{main.post(()->leverInFlight=false);}
            });
        }catch(RejectedExecutionException ex){leverInFlight=false;}
    }
    private long lastPress=0;
    private AlertDialog openDialog;
    @Override public void onCreate(Bundle saved){
        super.onCreate(saved);regular=getResources().getFont(R.font.stage_regular);bold=getResources().getFont(R.font.stage_bold);getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);getWindow().setStatusBarColor(BG);getWindow().setNavigationBarColor(BG);
        prefs=getSharedPreferences("remote",MODE_PRIVATE);loadLayout();try{snapshot=new JSONObject(prefs.getString("catalog","{}"));}catch(Exception ignored){}
        buildUi();RemoteClient.Config config=storedConfig();if(prefs.getBoolean("connected",false)&&config.valid())client=new RemoteClient(config);
        poller.scheduleWithFixedDelay(this::poll,0,450,TimeUnit.MILLISECONDS);
    }
    @Override protected void onResume(){super.onResume();foreground=true;refreshUi();}
    @Override protected void onPause(){endLever();foreground=false;ready=false;super.onPause();}
    @Override protected void onDestroy(){client=null;poller.shutdownNow();commands.shutdownNow();super.onDestroy();}
    private int dp(float n){return (int)(n*getResources().getDisplayMetrics().density+.5f);}
    private TextView text(String value,int size,int color){TextView t=new TextView(this);t.setTypeface(regular);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setGravity(Gravity.CENTER_VERTICAL);t.setLineSpacing(0,1.04f);return t;}
    private Button button(String value,String tag){Button b=new Button(this);b.setSoundEffectsEnabled(false);b.setTypeface(regular);b.setText(value);b.setTextSize(14);b.setTextColor(INK);b.setAllCaps(false);b.setTag(tag);b.setMinHeight(0);b.setMinimumHeight(dp(36));b.setPadding(dp(8),dp(4),dp(8),dp(4));b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x3372d6c2),shape(PANEL,0xff344150,1),null));return b;}
    private GradientDrawable shape(int color,int stroke,int width){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(8));d.setStroke(dp(width),stroke);return d;}
    private int blend(int from,int to,float amount){return Color.rgb((int)(Color.red(from)*(1-amount)+Color.red(to)*amount),(int)(Color.green(from)*(1-amount)+Color.green(to)*amount),(int)(Color.blue(from)*(1-amount)+Color.blue(to)*amount));}
    private LinearLayout row(){LinearLayout r=new LinearLayout(this);r.setOrientation(LinearLayout.HORIZONTAL);r.setGravity(Gravity.CENTER_VERTICAL);return r;}
    private ArrayAdapter<String> choices(List<String> values){return new ArrayAdapter<String>(this,android.R.layout.simple_spinner_item,values){
        private View label(int position,View recycled){TextView item=recycled instanceof TextView?(TextView)recycled:text("",14,INK);item.setText(getItem(position));item.setTypeface(regular);item.setTextSize(14);item.setSingleLine(false);item.setMaxLines(6);item.setPadding(dp(8),dp(8),dp(8),dp(8));item.setMinimumHeight(dp(44));item.setGravity(Gravity.CENTER_VERTICAL);item.setBackgroundColor(PANEL);return item;}
        @Override public View getView(int position,View recycled,ViewGroup parent){return label(position,recycled);}
        @Override public View getDropDownView(int position,View recycled,ViewGroup parent){return label(position,recycled);}
    };}
    private void weighted(LinearLayout row,View v,int height){v.setMinimumHeight(dp(height));LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(0,-2,1);p.setMargins(dp(3),dp(3),dp(3),dp(3));row.addView(v,p);}
    private void buildUi(){
        compact=getResources().getConfiguration().screenHeightDp<500;
        root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setBackgroundColor(BG);root.setPadding(dp(14),dp(6),dp(14),dp(6));
        root.setOnApplyWindowInsetsListener((v,insets)->{if(Build.VERSION.SDK_INT>=30){android.graphics.Insets system=insets.getInsets(WindowInsets.Type.systemBars()|WindowInsets.Type.displayCutout());root.setPadding(dp(14)+system.left,dp(6)+system.top,dp(14)+system.right,dp(6)+system.bottom);}else root.setPadding(dp(14)+insets.getSystemWindowInsetLeft(),dp(6)+insets.getSystemWindowInsetTop(),dp(14)+insets.getSystemWindowInsetRight(),dp(6)+insets.getSystemWindowInsetBottom());return insets;});
        LinearLayout header=row();TextView title=text("StagePilot",compact?22:24,INK);title.setTypeface(bold);title.setTag("brand");header.addView(title,new LinearLayout.LayoutParams(0,-2,1));edit=button("編集","edit");edit.setContentDescription("ボタンの配置を編集");header.addView(edit,new LinearLayout.LayoutParams(dp(64),-2));Button connect=button("接続","connect");LinearLayout.LayoutParams cp=new LinearLayout.LayoutParams(dp(64),-2);cp.setMargins(dp(8),0,0,0);header.addView(connect,cp);header.setMinimumHeight(dp(compact?38:46));root.addView(header);connect.setOnClickListener(v->connectionDialog());edit.setOnClickListener(v->{endLever();editing=!editing;if(!editing)feedback.setText("ボタンを押すとPCを操作します");refreshUi();});
        status=text("未接続 / PCのスマホ連携を開始してください",12,MUTED);status.setTag("status");status.setMaxLines(2);root.addView(status,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout current=row();active=text("現在：—",compact?16:20,INK);active.setTypeface(bold);active.setTag("active");active.setPadding(0,dp(compact?0:6),dp(8),dp(compact?0:6));current.addView(active,new LinearLayout.LayoutParams(0,-2,1));activeShift=text("",12,0xffffbb70);activeShift.setTag("active-shift");current.addView(activeShift,new LinearLayout.LayoutParams(-2,-2));root.addView(current);
        LinearLayout pages=row();for(int i=0;i<3;++i){final int bank=i;pageButtons[i]=button(pageNames[i],"page"+i);weighted(pages,pageButtons[i],compact?30:40);pageButtons[i].setOnClickListener(v->{page=bank;rebuildGrid();refreshUi();});pageButtons[i].setOnLongClickListener(v->{if(!editing)return false;EditText name=new EditText(this);name.setText(pageNames[bank]);new AlertDialog.Builder(this).setTitle("ページの名前").setView(name).setPositiveButton("保存",(d,w)->{String value=name.getText().toString().trim();if(!value.isEmpty())pageNames[bank]=value.substring(0,Math.min(12,value.length()));saveLayout();refreshUi();}).setNegativeButton("取消",null).show();return true;});}layoutButton=button("配置","layout");layoutButton.setVisibility(View.GONE);pages.addView(layoutButton,new LinearLayout.LayoutParams(dp(64),-2));layoutButton.setOnClickListener(v->layoutDialog());root.addView(pages);
        ScrollView scroll=new ScrollView(this);scroll.setTag("pad-scroll");scroll.setFillViewport(false);grid=new GridLayout(this);grid.setColumnCount(getResources().getConfiguration().screenWidthDp>=650?4:2);scroll.addView(grid,new ScrollView.LayoutParams(-1,-2));root.addView(scroll,new LinearLayout.LayoutParams(-1,0,1));
        rebuildGrid();
        feedback=text("ボタンを押すとPCを操作します",compact?11:12,MUTED);feedback.setTag("feedback");feedback.setPadding(0,dp(compact?0:4),0,dp(compact?0:4));root.addView(feedback,new LinearLayout.LayoutParams(-1,-2));
        LinearLayout volumeRow=row();volumeRow.setMinimumHeight(dp(compact?34:42));volumeLabel=text("全体 70%",13,INK);volumeLabel.setMaxLines(2);volumeRow.addView(volumeLabel,new LinearLayout.LayoutParams(dp(84),-2));volume=new SeekBar(this);volume.setTag("volume");volume.setMax(100);volumeRow.addView(volume,new LinearLayout.LayoutParams(0,dp(34),1));Button reset=button("70%","reset-volume");reset.setContentDescription("音量を標準の70%に戻す");volumeRow.addView(reset,new LinearLayout.LayoutParams(dp(60),-2));reset.setOnClickListener(v->send("master",0,.7f));root.addView(volumeRow);
        volume.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar s,int progress,boolean user){if(user)volumeLabel.setText("全体 "+progress+"%");}public void onStartTrackingTouch(SeekBar s){volumeTracking=true;}public void onStopTrackingTouch(SeekBar s){volumeTracking=false;if(!updatingVolume)send("master",0,s.getProgress()/100f);}});
        setContentView(root);refreshUi();
    }
    private void rebuildGrid(){
        if(grid==null)return;endLever();grid.removeAllViews();Arrays.fill(padButtons,null);int columns=columnCounts[page]==0?(getResources().getConfiguration().screenWidthDp>=650?4:2):columnCounts[page];grid.setColumnCount(columns);
        for(int i=0;i<padCounts[page];++i){final int at=i;PadButton b=new PadButton("pad"+i);GridLayout.LayoutParams p=new GridLayout.LayoutParams();p.columnSpec=GridLayout.spec(i%grid.getColumnCount(),1,1f);p.rowSpec=GridLayout.spec(i/grid.getColumnCount(),GridLayout.FILL);p.width=0;p.height=-2;p.setMargins(dp(4),dp(4),dp(4),dp(4));grid.addView(b,p);padButtons[i]=b;b.setOnClickListener(v->{int slot=page*MAX_PADS+at;if(editing||pads[slot].optString("action").isEmpty())editPad(slot);else if(!isLever(pads[slot].optString("action")))triggerPad(slot);});b.setOnLongClickListener(v->{if(editing)editPad(page*MAX_PADS+at);else{JSONObject pad=pads[page*MAX_PADS+at];new AlertDialog.Builder(this).setTitle(pad.optString("name","未設定")).setMessage(isTarget(pad.optString("action"))?actualName(pad.optString("action"),pad.optInt("index")):"「編集」で名前・操作・色を変更できます。").setPositiveButton("閉じる",null).show();}return true;});}
    }
    private void layoutDialog(){
        int bank=page;LinearLayout form=new LinearLayout(this);form.setOrientation(LinearLayout.VERTICAL);form.setPadding(dp(20),dp(8),dp(20),dp(8));
        form.addView(text("「"+pageNames[bank]+"」の配置",16,INK));form.addView(text("列数",14,MUTED));Spinner columns=new Spinner(this);columns.setAdapter(choices(Arrays.asList("自動（縦2列・横4列）","1列","2列","3列","4列")));columns.setSelection(columnCounts[bank]);columns.setTag("layout-columns");form.addView(columns);
        form.addView(text("ボタンの数",14,MUTED));Spinner count=new Spinner(this);ArrayList<String> counts=new ArrayList<>();for(int n=1;n<=MAX_PADS;++n)counts.add(n+"個");count.setAdapter(choices(counts));count.setSelection(padCounts[bank]-1);count.setTag("layout-count");form.addView(count);form.addView(text("最大32個 × 3ページ。増えた分はスクロールして使えます。数を減らして隠れたボタンも、増やし直すと元に戻ります。",13,MUTED));
        ScrollView scroll=new ScrollView(this);scroll.addView(form);openDialog=new AlertDialog.Builder(this).setTitle("グリッドの配置").setView(scroll).setPositiveButton("保存",(d,w)->{columnCounts[bank]=columns.getSelectedItemPosition();padCounts[bank]=count.getSelectedItemPosition()+1;saveLayout();rebuildGrid();refreshUi();}).setNegativeButton("取消",null).create();openDialog.show();
    }
    private RemoteClient.Config storedConfig(){return new RemoteClient.Config(prefs.getString("host",""),prefs.getInt("port",18760),prefs.getString("token",""),prefs.getInt("mode",0));}
    static RemoteClient.Config parsePairing(String payload,int mode) {
        if(payload==null||payload.length()>220)return null;
        Uri uri=Uri.parse(payload.trim());
        if(!"stagepilot".equals(uri.getScheme())||uri.getHost()==null||uri.getUserInfo()!=null||
           uri.getQuery()!=null||uri.getFragment()!=null||uri.getPathSegments().size()!=1)return null;
        String key=uri.getLastPathSegment();
        if(key==null||!key.matches("[A-Za-z0-9_-]{8,128}"))return null;
        RemoteClient.Config config=new RemoteClient.Config(uri.getHost(),uri.getPort(),key,mode);
        return config.valid()?config:null;
    }
    @Override protected void onActivityResult(int requestCode,int resultCode,Intent data){
        IntentResult result=IntentIntegrator.parseActivityResult(requestCode,resultCode,data);
        if(result==null){super.onActivityResult(requestCode,resultCode,data);return;}
        if(result.getContents()==null)return;
        RemoteClient.Config config=parsePairing(result.getContents(),storedConfig().mode);
        if(config==null){Toast.makeText(this,"StagePilotの接続QRを読み取ってください",Toast.LENGTH_LONG).show();return;}
        if(openDialog!=null)openDialog.dismiss();
        activate(config);
    }
    private void bluetoothGuide(){
        new AlertDialog.Builder(this).setTitle("Bluetoothで接続")
            .setMessage("① PCとスマホをBluetoothでペアリングします。\n② スマホのテザリング設定で「Bluetoothテザリング」をオンにします。\n③ Windowsでスマホの「PANに参加」を選びます。\n④ PCのスマホ連携でBluetooth側のIPを選び、QRを読みます。")
            .setPositiveButton("Bluetooth設定を開く",(d,w)->startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS)))
            .setNegativeButton("閉じる",null).show();
    }
    private void connectionDialog(){
        LinearLayout form=new LinearLayout(this);form.setOrientation(LinearLayout.VERTICAL);form.setPadding(dp(20),dp(8),dp(20),dp(8));RemoteClient.Config old=storedConfig();
        TextView hint=text("同じWi-Fi、USBまたはBluetoothテザリングでつないでから、PC側で「スマホ連携」を開始します。",15,INK);form.addView(hint);
        Button qr=button("QRを読む","scan-qr");form.addView(qr);
        qr.setOnClickListener(v->new IntentIntegrator(this).setDesiredBarcodeFormats(IntentIntegrator.QR_CODE)
            .setPrompt("PCの「スマホ連携」に表示したQRを読み取ります")
            .setOrientationLocked(false).setBeepEnabled(false).initiateScan());
        Button bluetooth=button("Bluetoothで接続する手順","bluetooth-guide");form.addView(bluetooth);
        bluetooth.setOnClickListener(v->bluetoothGuide());
        form.addView(text("手動で入力",13,MUTED));
        EditText host=new EditText(this);host.setSingleLine(true);host.setHint("PCのIPアドレス  例：192.168.1.10");host.setText(old.host);host.setTag("host");form.addView(host);EditText port=new EditText(this);port.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);port.setText(""+old.port);port.setHint("基本ポート");port.setTag("port");form.addView(port);EditText token=new EditText(this);token.setSingleLine(true);token.setHint("PCに表示される接続キー");token.setText(old.token);token.setTag("token");form.addView(token);
        Spinner mode=new Spinner(this);mode.setAdapter(choices(Arrays.asList("HTTP / おすすめ・確認応答あり","TCP / 確認応答あり","OSC / UDP / 再送と重複除去あり")));mode.setSelection(old.mode);mode.setTag("mode");form.addView(text("通信方式",13,MUTED));form.addView(mode);Button paste=button("接続情報を貼り付け","paste");form.addView(paste);paste.setOnClickListener(v->{try{android.content.ClipboardManager clipboard=(android.content.ClipboardManager)getSystemService(CLIPBOARD_SERVICE);String s=clipboard.getPrimaryClip().getItemAt(0).coerceToText(this).toString();RemoteClient.Config config=parsePairing(s,mode.getSelectedItemPosition());if(config==null)throw new IllegalArgumentException();host.setText(config.host);port.setText(""+config.port);token.setText(config.token);}catch(Exception ex){Toast.makeText(this,"PCの「接続情報をコピー」で取得した内容を貼り付けてください",Toast.LENGTH_LONG).show();}});
        ScrollView scroll=new ScrollView(this);scroll.addView(form);AlertDialog dialog=new AlertDialog.Builder(this).setTitle("PCに接続").setView(scroll).setPositiveButton("接続",null).setNegativeButton("取消",null).setNeutralButton("接続解除",(d,w)->{client=null;ready=false;commands.getQueue().clear();prefs.edit().putBoolean("connected",false).apply();refreshUi();}).create();openDialog=dialog;dialog.setOnShowListener(d->{for(int which:new int[]{-1,-2,-3})dialog.getButton(which).setSoundEffectsEnabled(false);dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v->{int number;try{number=Integer.parseInt(port.getText().toString());}catch(Exception ex){number=0;}RemoteClient.Config c=new RemoteClient.Config(host.getText().toString().trim(),number,token.getText().toString().trim(),mode.getSelectedItemPosition());if(!c.valid()){Toast.makeText(this,"IP・ポート・接続キーを確認してください",Toast.LENGTH_LONG).show();return;}activate(c);dialog.dismiss();});});dialog.show();
    }
    void activate(RemoteClient.Config config){ready=false;failures=0;commands.getQueue().clear();client=new RemoteClient(config);prefs.edit().putString("host",config.host).putInt("port",config.port).putString("token",config.token).putInt("mode",config.mode).putBoolean("connected",true).apply();feedback.setText("接続を確認中…");refreshUi();}
    private void poll(){RemoteClient target=client;if(!foreground||target==null)return;try{JSONObject state=target.state();main.post(()->{if(client!=target||!foreground)return;snapshot=state;ready=state.optBoolean("ready",false);failures=0;String catalog=String.valueOf(state.optJSONArray("items"))+state.optString("session");if(!catalog.equals(cachedCatalog)){cachedCatalog=catalog;prefs.edit().putString("catalog",state.toString()).apply();}if(!prefs.getBoolean("initialized",false)){autoLayout(state);prefs.edit().putBoolean("initialized",true).apply();}refreshUi();});}catch(Exception ex){main.post(()->{if(client!=target||!foreground)return;ready=false;++failures;refreshUi();status.setText("切断 / "+friendlyError(ex));});}}
    private String friendlyError(Exception ex){if(ex instanceof java.net.SocketTimeoutException||ex instanceof java.net.ConnectException||ex instanceof java.net.UnknownHostException)return "PCの連携開始と、Wi-Fi・テザリングの接続を確認してください";return ex.getMessage()==null?"通信できません":ex.getMessage();}
    private void autoLayout(JSONObject state){
        JSONArray items=state.optJSONArray("items");if(items==null)return;int sound=0,effect=MAX_PADS;
        // A single performance page follows the same instrument-then-scene order as the PC.
        for(String kind:new String[]{"instrument","scene"})for(int i=0;i<items.length();++i){JSONObject item=items.optJSONObject(i);if(item!=null&&kind.equals(item.optString("kind"))&&sound<16)pads[sound++]=newPad(item.optString("name"),kind,item.optInt("index"),item.optString("color","72d6c2"));}
        for(int i=0;i<items.length();++i){JSONObject item=items.optJSONObject(i);if(item!=null&&item.optString("kind").equals("effect")&&effect<MAX_PADS+8)pads[effect++]=newPad(item.optString("name"),"effect",item.optInt("index"),item.optString("color","72d6c2"));}
        padCounts[0]=Math.max(8,sound);pads[MAX_PADS+6]=newPad("効果音停止","effectsStop",0,"ff9299");pads[MAX_PADS+7]=newPad("ゆっくり消音","fade",0,"94baff");
        pads[MAX_PADS*2]=newPad("次の音色","next",0,"72d6c2");pads[MAX_PADS*2+1]=newPad("前の音色","previous",0,"72d6c2");pads[MAX_PADS*2+2]=newPad("移調 0 半音","transpose",0,"ffbb70");pads[MAX_PADS*2+3]=newPad("移調を＋1半音","transposeUp",0,"72d6c2");pads[MAX_PADS*2+4]=newPad("移調を−1半音","transposeDown",0,"72d6c2");pads[MAX_PADS*2+6]=newPad("音量を戻す","restore",0,"72d6c2");pads[MAX_PADS*2+7]=newPad("全音停止","panic",0,"ff9299");saveLayout();rebuildGrid();
    }
    private static JSONObject newPad(String name,String action,int index,String color){JSONObject pad=new JSONObject();try{pad.put("name",name).put("action",action).put("index",index).put("color",color);}catch(Exception ignored){}return pad;}
    private void loadLayout(){
        for(int i=0;i<pads.length;++i)pads[i]=new JSONObject();
        try{boolean modern=prefs.contains("padsV2");JSONArray values=new JSONArray(prefs.getString(modern?"padsV2":"pads","[]"));for(int i=0;i<(modern?pads.length:24);++i){JSONObject value=values.optJSONObject(i);if(value!=null)pads[modern?i:(i/8)*MAX_PADS+i%8]=value;}
            if(values.length()>0&&!prefs.getBoolean("initialized",false))prefs.edit().putBoolean("initialized",true).apply();
            JSONArray pages=new JSONArray(prefs.getString("pages","[]")),counts=new JSONArray(prefs.getString("padCounts","[]")),columns=new JSONArray(prefs.getString("columnCounts","[]"));for(int i=0;i<3;++i){pageNames[i]=pages.optString(i,pageNames[i]);padCounts[i]=Math.max(1,Math.min(MAX_PADS,counts.optInt(i,8)));columnCounts[i]=Math.max(0,Math.min(4,columns.optInt(i,0)));}
        }catch(JSONException ignored){}
    }
    private void saveLayout(){JSONArray data=new JSONArray(),pages=new JSONArray(),counts=new JSONArray(),columns=new JSONArray();for(JSONObject p:pads)data.put(p);for(int i=0;i<3;++i){pages.put(pageNames[i]);counts.put(padCounts[i]);columns.put(columnCounts[i]);}prefs.edit().putBoolean("initialized",true).putString("padsV2",data.toString()).putString("pages",pages.toString()).putString("padCounts",counts.toString()).putString("columnCounts",columns.toString()).apply();}
    private JSONObject findTarget(String action,int index){JSONArray items=snapshot.optJSONArray("items");if(items!=null)for(int i=0;i<items.length();++i){JSONObject o=items.optJSONObject(i);if(o!=null&&o.optString("kind").equals(action)&&o.optInt("index")==index)return o;}return null;}
    private boolean isTarget(String action){return action.equals("scene")||action.equals("instrument")||action.equals("effect");}
    private static String shiftLabel(int value){return (value>0?"+":"")+value+" 半音";}
    private String kindLabel(String kind){return switch(kind){case "scene","instrument"->"音色";case "effect"->"効果音";default->"操作";};}
    private String actualName(String action,int index){JSONObject target=findTarget(action,index);return target==null?"PCのセットにありません":target.optString("name");}
    private void refreshUi(){
        if(root==null)return;RemoteClient target=client;String protocol=target==null?"":new String[]{"HTTP","TCP","OSC / UDP"}[target.config().mode];status.setText(target==null?"未接続 · 右上の「接続」から始めます":ready?"● 接続中 · "+protocol+" · "+snapshot.optString("session"):failures>0?"切断 · 操作は送信しません":"接続を確認中 / PCの準備待ち");status.setTextColor(ready?MINT:MUTED);
        int scene=snapshot.optInt("scene",-1),instrument=snapshot.optInt("instrument",0),shift=snapshot.optInt("transpose",0);active.setText("現在："+(target==null||!ready?"—":actualName(scene>=0?"scene":"instrument",scene>=0?scene:instrument)));activeShift.setText("全体移調 "+shiftLabel(shift));activeShift.setVisibility(ready?View.VISIBLE:View.GONE);edit.setText(editing?"完了":"編集");layoutButton.setVisibility(editing?View.VISIBLE:View.GONE);if(edit.isSelected()!=editing){edit.setSelected(editing);edit.setBackground(shape(editing?0xff286448:PANEL,0xff344150,1));}
        for(int i=0;i<3;++i){pageButtons[i].setText(pageNames[i]);boolean selected=i==page;if(pageButtons[i].isSelected()!=selected){pageButtons[i].setSelected(selected);pageButtons[i].setBackground(shape(selected?0xff263d38:PANEL,selected?MINT:0xff344150,1));}pageButtons[i].setTextColor(selected?MINT:MUTED);}
        JSONArray playing=snapshot.optJSONArray("playing");for(int i=0;i<padCounts[page];++i){JSONObject p=pads[page*MAX_PADS+i];String action=p.optString("action"),name=p.optString("name",""),color=p.optString("color","72d6c2");int index=p.optInt("index");boolean empty=action.isEmpty(),missing=isTarget(action)&&findTarget(action,index)==null,oldStep=isStep(action)&&!snapshot.optBoolean("transposeStep");missing|=oldStep;boolean selected=ready&&((action.equals("scene")&&scene==index)||(action.equals("instrument")&&scene<0&&instrument==index)||(action.equals("transpose")&&shift==p.optInt("value",0)));boolean isPlaying=false;if(playing!=null&&action.equals("effect"))for(int n=0;n<playing.length();++n)isPlaying|=playing.optInt(n)==index;int accent;try{accent=Color.parseColor("#"+color);}catch(Exception ex){accent=MINT;}
            String description=empty?"":missing?(oldStep?"PC版0.18へ更新してください":"PCのセットにありません"):isTarget(action)?kindLabel(action)+(action.equals("effect")?" "+(index+1):"")+(name.equals(actualName(action,index))?"":"\n"+actualName(action,index)):action.equals("transpose")?"全体移調 → "+shiftLabel(p.optInt("value",0)):isStep(action)?"全体移調を"+(action.equals("transposeUp")?"＋1":"−1")+"半音":"操作ボタン";
            String message=empty?(editing?"押して作成":"押して割り当て"):editing?"押して編集":!ready?"未接続":selected?(action.equals("transpose")?"適用中":"演奏中"):isPlaying?"再生中":action.equals("effect")?"押して再生":isTarget(action)||action.equals("transpose")?"押して切替":"押して操作";
            JSONArray extra=p.optJSONArray("extra");
            boolean pitch=action.equals("pitch"), wheel=isWheel(action), lever=isLever(action), advanced=pitch||isVolume(action)||(extra!=null&&extra.length()>0);
            if(advanced&&!snapshot.optBoolean("remoteControls")){missing=true;description="PC版0.21に更新してください";}
            else if(wheel){
                description=action.equals("modulation")?"モジュレーション · CC1":"エクスプレッション · CC11";
                message=editing?"押して編集":ready?"離した位置を保持":"未接続";
                if(!snapshot.optBoolean("remoteWheels")){missing=true;description="PC版0.22に更新してください";}
            }
            else if(pitch){description="ピッチベンド";message=editing?"押して編集":ready?"離すと中央に戻ります":"未接続";}
            else if(extra!=null&&extra.length()>0){description=(extra.length()+1)+"つの操作";if(ready&&!editing)message="押してまとめて実行";}
            else if(isVolume(action))description=p.optInt("value")+"% "+(action.equals("masterStep")?"増減":"に設定");
            SeekBar slider=padButtons[i].lever;
            if(lever&&!action.equals(slider.getTag(R.id.lever_action))){
                slider.setTag(R.id.lever_action,action);
                slider.setMax(pitch?16383:127);
                slider.setProgress(pitch?8192:action.equals("expression")?127:0);
                slider.setContentDescription(pitch?"ピッチベンド":action.equals("modulation")?"モジュレーション":"エクスプレッション");
            }
            if(wheel&&activeLever!=slider&&SystemClock.elapsedRealtime()>=wheelPendingUntil)
                slider.setProgress(snapshot.optInt(action,action.equals("expression")?127:0));
            slider.setVisibility(lever&&!editing?View.VISIBLE:View.GONE);
            slider.setEnabled(ready&&!missing);
            padButtons[i].display(empty?"未設定":name,description,message,empty?MUTED:accent,selected||isPlaying,empty);padButtons[i].setEnabled(editing||empty||(ready&&!missing));padButtons[i].setAlpha(!editing&&!empty&&(!ready||missing)?.75f:1f);}
        volume.setEnabled(ready);if(!volumeTracking){updatingVolume=true;int value=(int)Math.round(snapshot.optDouble("master",.7)*100);volume.setProgress(value);volumeLabel.setText("全体 "+value+"%");updatingVolume=false;}if(editing)feedback.setText("ボタンを押すと名前・操作・色を変更できます。ページ名は長押しで変更。");
    }
    private boolean isVolume(String action){return action.equals("master")||action.equals("masterStep")||action.equals("slotGain");}
    private final class OperationEditor {
        final LinearLayout view,values;
        final Spinner action,value;
        final ArrayList<JSONObject> options=new ArrayList<>();
        final ArrayList<String> labels=new ArrayList<>();
        int minimum;
        OperationEditor(JSONObject current,boolean primary){
            view=new LinearLayout(MainActivity.this);view.setOrientation(LinearLayout.VERTICAL);
            JSONArray items=snapshot.optJSONArray("items");
            if(items!=null)for(int i=0;i<items.length();++i){
                JSONObject item=items.optJSONObject(i);
                if(item==null)continue;
                String kind=item.optString("kind"), name=item.optString("name");
                option(kindLabel(kind)+" · "+name,kind,item.optInt("index"));
            }
            for(int i=0;i<UTILITY_ACTIONS.length;++i)
                if(primary||!isLever(UTILITY_ACTIONS[i]))option(UTILITY_NAMES[i],UTILITY_ACTIONS[i],0);
            if(items!=null)for(int i=0;i<items.length();++i){
                JSONObject item=items.optJSONObject(i);
                if(item!=null&&item.optString("kind").equals("instrument"))
                    option("音源の音量 · "+item.optString("name"),"slotGain",item.optInt("index"));
            }
            int initial=0;
            String previous=current.optString("action");
            if(!previous.isEmpty()){
                initial=-1;
                for(int i=0;i<options.size();++i)if(options.get(i).optString("action").equals(previous)&&options.get(i).optInt("index")==current.optInt("index")){initial=i;break;}
                if(initial<0){initial=options.size();option("保存済みの操作（PCの一覧にありません）",previous,current.optInt("index"));}
            }
            action=new Spinner(MainActivity.this);action.setAdapter(choices(labels));action.setSelection(initial);
            if(primary)action.setTag("pad-action");
            view.addView(action);
            values=new LinearLayout(MainActivity.this);values.setOrientation(LinearLayout.VERTICAL);
            value=new Spinner(MainActivity.this);if(primary)value.setTag("pad-transpose");
            values.addView(value);view.addView(values);
            final int startingIndex=initial;
            configureValue(options.get(initial).optString("action"),current);
            action.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener(){
                int last=startingIndex;
                public void onItemSelected(AdapterView<?> parent,View child,int position,long id){
                    if(position!=last){last=position;configureValue(options.get(position).optString("action"),new JSONObject());}
                    if(primary&&afterPrimaryChange!=null)afterPrimaryChange.run();
                }
                public void onNothingSelected(AdapterView<?> parent){}
            });
        }
        Runnable afterPrimaryChange;
        void option(String label,String key,int index){labels.add(label);options.add(newPad(label,key,index,"72d6c2"));}
        String key(){return options.get(action.getSelectedItemPosition()).optString("action");}
        void configureValue(String key,JSONObject previous){
            boolean shift=key.equals("transpose"), step=key.equals("masterStep");
            values.setVisibility(shift||isVolume(key)?View.VISIBLE:View.GONE);
            if(!shift&&!isVolume(key))return;
            minimum=shift?-24:step?-100:0;
            int max=shift?24:100;
            ArrayList<String> labels=new ArrayList<>();
            for(int n=minimum;n<=max;++n)labels.add(shift?shiftLabel(n):((step&&n>0?"+":"")+n+"%"));
            value.setAdapter(choices(labels));
            value.setSelection(Math.max(0,Math.min(max-minimum,previous.optInt("value",shift?0:step?5:70)-minimum)));
        }
        JSONObject operation() throws JSONException {
            JSONObject choice=options.get(action.getSelectedItemPosition());
            JSONObject operation=new JSONObject().put("action",key()).put("index",choice.optInt("index"));
            if(key().equals("transpose")||isVolume(key()))
                operation.put("value",minimum+value.getSelectedItemPosition());
            return operation;
        }
    }
    private void editPad(int slot){
        JSONObject current=pads[slot];
        LinearLayout form=new LinearLayout(this);form.setOrientation(LinearLayout.VERTICAL);form.setPadding(dp(20),dp(8),dp(20),dp(8));
        EditText name=new EditText(this);name.setSingleLine(true);name.setHint("表示する名前");name.setText(current.optString("name"));name.setTag("pad-name");form.addView(name);
        form.addView(text("最初の操作",14,MUTED));
        OperationEditor first=new OperationEditor(current,true);
        form.addView(first.view);
        LinearLayout more=new LinearLayout(this);more.setOrientation(LinearLayout.VERTICAL);form.addView(more);
        ArrayList<OperationEditor> editors=new ArrayList<>();
        Button append=button("次の操作を追加","add-operation");form.addView(append);
        Runnable visibility=()->{
            boolean button=!isLever(first.key());
            more.setVisibility(button?View.VISIBLE:View.GONE);
            append.setVisibility(button&&editors.size()<7?View.VISIBLE:View.GONE);
        };
        java.util.function.Consumer<JSONObject> addOperation=saved->{
            OperationEditor editor=new OperationEditor(saved,false);
            editors.add(editor);
            editor.action.setTag("extra-action-"+editors.size());
            editor.value.setTag("extra-value-"+editors.size());
            LinearLayout group=new LinearLayout(this);group.setOrientation(LinearLayout.VERTICAL);
            LinearLayout header=row();weighted(header,text("続けて実行",14,MUTED),32);
            Button remove=button("削除","remove-operation");header.addView(remove);
            group.addView(header);group.addView(editor.view);more.addView(group);
            remove.setOnClickListener(v->{editors.remove(editor);more.removeView(group);visibility.run();});
            visibility.run();
        };
        JSONArray saved=current.optJSONArray("extra");
        if(saved!=null)for(int i=0;i<Math.min(7,saved.length());++i)if(saved.optJSONObject(i)!=null)addOperation.accept(saved.optJSONObject(i));
        append.setOnClickListener(v->addOperation.accept(new JSONObject()));
        first.afterPrimaryChange=visibility;visibility.run();
        Spinner color=new Spinner(this);color.setAdapter(choices(Arrays.asList(COLOUR_NAMES)));
        for(int i=0;i<COLOURS.length;++i)if(COLOURS[i].equals(current.optString("color")))color.setSelection(i);
        color.setTag("pad-color");form.addView(text("色",14,MUTED));form.addView(color);
        ScrollView scroll=new ScrollView(this);scroll.addView(form);
        openDialog=new AlertDialog.Builder(this).setTitle("枠 "+(slot%MAX_PADS+1)+" を編集").setView(scroll)
            .setPositiveButton("保存",null).setNeutralButton("割り当てを外す",(d,w)->{pads[slot]=new JSONObject();saveLayout();refreshUi();})
            .setNegativeButton("取消",null).create();
        openDialog.setOnShowListener(d->{
            for(int which:new int[]{-1,-2,-3})openDialog.getButton(which).setSoundEffectsEnabled(false);
            openDialog.getButton(-1).setOnClickListener(v->{
                try{
                    JSONObject pad=first.operation();
                    String caption=name.getText().toString().trim();
                    if(caption.isEmpty())caption=first.key().equals("transpose")?
                        "移調 "+shiftLabel(pad.optInt("value")):first.labels.get(first.action.getSelectedItemPosition());
                    pad.put("name",caption.substring(0,Math.min(caption.length(),24))).put("color",COLOURS[color.getSelectedItemPosition()]);
                    JSONArray extra=new JSONArray();
                    if(!isLever(first.key()))for(OperationEditor editor:editors)extra.put(editor.operation());
                    if(extra.length()>0)pad.put("extra",extra);
                    pads[slot]=pad;saveLayout();refreshUi();openDialog.dismiss();
                }catch(JSONException ex){Toast.makeText(this,"操作を保存できませんでした",Toast.LENGTH_SHORT).show();}
            });
        });
        openDialog.show();
    }
    private boolean isStep(String action){return action.equals("transposeUp")||action.equals("transposeDown");}
    private JSONObject commandFor(JSONObject operation) throws JSONException {
        String key=operation.optString("action");
        float value=isStep(key)?(key.equals("transposeUp")?1:-1):
            isVolume(key)?operation.optInt("value")/100f:key.equals("transpose")?operation.optInt("value"):1;
        return new JSONObject().put("action",isStep(key)?"transposeStep":key).put("index",operation.optInt("index")).put("value",value);
    }
    private void triggerPad(int slot){
        JSONObject pad=pads[slot];
        String action=pad.optString("action");
        if(!isStep(action)&&!action.equals("transpose")&&!isVolume(action)&&SystemClock.elapsedRealtime()-lastPress<100)return;
        lastPress=SystemClock.elapsedRealtime();
        try{
            JSONObject primary=commandFor(pad);
            JSONArray extra=pad.optJSONArray("extra");
            if(extra==null||extra.length()==0){
                send(primary.getString("action"),primary.getInt("index"),(float)primary.getDouble("value"));
                return;
            }
            JSONArray operations=new JSONArray().put(primary);
            for(int i=0;i<extra.length();++i)operations.put(commandFor(extra.getJSONObject(i)));
            send("batch:"+operations,0,1);
        }catch(JSONException ex){feedback.setText("割り当てを開いて保存し直してください");}
    }
    private void send(String action,int index,float value){RemoteClient target=client;if(!ready||target==null){feedback.setText("未接続です。右上の「接続」を確認してください");return;}long pressed=SystemClock.elapsedRealtime();feedback.setText("送信中…");try{commands.execute(()->{if(client!=target||!foreground||SystemClock.elapsedRealtime()-pressed>700){main.post(()->feedback.setText("古い操作を取り消しました。もう一度押してください"));return;}try{target.command(action,index,value);main.post(()->{if(client==target){feedback.setText("PCが操作を受け付けました");poller.execute(this::poll);}});}catch(Exception ex){main.post(()->{if(client==target){feedback.setText("結果未確認 / "+friendlyError(ex));poller.execute(this::poll);}});}});}catch(RejectedExecutionException ex){feedback.setText("操作待ちが多いため取り消しました。もう一度押してください");}}
    // The instrumentation exercises these same controls and production network paths.
    boolean testReady(){return ready;}
    JSONObject testState(){return snapshot;}
    void testPage(int p){page=p;rebuildGrid();refreshUi();}
    void testPad(int p){padButtons[p].performClick();}
    View testView(String tag){return root.findViewWithTag(tag);}
    String testLayout(){return prefs.getString("padsV2","[]");}
    View testDialogView(String tag){return openDialog.getWindow().getDecorView().findViewWithTag(tag);}
    void testDialogSave(){openDialog.getButton(AlertDialog.BUTTON_POSITIVE).performClick();}
    int testColumns(){return grid.getColumnCount();}
    boolean testAllPadsVisible(){return padButtons[7].getBottom()<=((View)grid.getParent()).getHeight();}
    private boolean textFits(TextView view,boolean complete){android.text.Layout layout=view.getLayout();if(layout==null||layout.getLineCount()==0)return false;boolean fits=layout.getHeight()<=view.getHeight()-view.getCompoundPaddingTop()-view.getCompoundPaddingBottom()+1;if(complete){fits&=layout.getLineEnd(layout.getLineCount()-1)>=view.getText().length();for(int line=0;line<layout.getLineCount();++line)fits&=layout.getEllipsisCount(line)==0;}return fits;}
    boolean testTextFits(){boolean fits=textFits((TextView)testView("brand"),true)&&textFits(active,true)&&textFits(volumeLabel,true);for(PadButton button:padButtons)if(button!=null)fits&=textFits(button.caption,true)&&textFits(button.state,true)&&button.caption.getBottom()<=((View)button.caption.getParent()).getHeight();for(Button button:pageButtons)fits&=textFits(button,true);return fits;}
}
