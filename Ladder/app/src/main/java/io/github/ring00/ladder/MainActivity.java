package io.github.ring00.ladder;

import android.content.Intent;
import android.net.VpnService;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import butterknife.BindView;
import butterknife.ButterKnife;

import static java.lang.Thread.sleep;

public class MainActivity extends AppCompatActivity implements View.OnClickListener, Runnable {

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    static final String TAG = MainActivity.class.getSimpleName();

    @BindView(R.id.address_box) LinearLayout addressBox;
    @BindView(R.id.port_box) LinearLayout portBox;

    @BindView(R.id.address) EditText addressText;
    @BindView(R.id.port) EditText portText;

    @BindView(R.id.statistics) TextView statistics;
    @BindView(R.id.button) Button button;

    boolean connected = false;
    Thread updater = null;

    public native String getStatistics();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ButterKnife.bind(this);

        button.setOnClickListener(this);
    }

    private void changeVisibility(boolean connected) {
        if (connected) {
            button.setText(R.string.connect);

            addressBox.setVisibility(View.VISIBLE);
            portBox.setVisibility(View.VISIBLE);
            statistics.setVisibility(View.GONE);
        } else {
            button.setText(R.string.disconnect);

            addressBox.setVisibility(View.GONE);
            portBox.setVisibility(View.GONE);
            statistics.setVisibility(View.VISIBLE);
        }
    }

    @Override
    public void onClick(View view) {
        Button button = (Button) view;
        changeVisibility(connected);
        if (connected) {
            connected = false;

            if (updater != null) {
                updater.interrupt();
            }
        } else {
            connected = true;

            Intent intent = VpnService.prepare(getApplicationContext());
            if (intent != null) {
                startActivityForResult(intent, 0);
            } else {
                onActivityResult(0, RESULT_OK, null);
            }

//            updater = new Thread(this);
//            runOnUiThread(updater);
//            updater.start();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        Log.d(TAG, "onActivityResult: ");
        if (resultCode == RESULT_OK) {
            Intent intent = new Intent(this, LadderService.class);
            intent.putExtra(getString(R.string.server_address), addressText.getText().toString());
            intent.putExtra(getString(R.string.server_port), portText.getText().toString());
            startService(intent);
        }
    }

    @Override
    public void run() {
        while (connected) {
            try {
                sleep(1000);
            } catch (InterruptedException e) {
                break;
            }

            String rawdata = getStatistics();
            String[] data = rawdata.split(" ");

            int bytesIn = Integer.parseInt(data[0]);
            int packetsIn = Integer.parseInt(data[1]);
            int flowIn = Integer.parseInt(data[2]);
            int timeIn = Integer.parseInt(data[3]);

            int bytesOut = Integer.parseInt(data[4]);
            int packetsOut = Integer.parseInt(data[5]);
            int flowOut = Integer.parseInt(data[6]);
            int timeOut = Integer.parseInt(data[7]);

            String output = String.format("Total download: %d Bytes, %d Packets\nTotal upload: %d Bytes, %d Packets\nDownload speed: %.1f Bytes/s\nUploading speed: %.1f Bytes/s", bytesIn, packetsIn, bytesOut, packetsOut, (float)flowIn / timeIn, (float)flowOut / timeOut);
            statistics.setText(output);
        }
    }
}
