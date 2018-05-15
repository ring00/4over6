package io.github.ring00.ladder;

import android.content.Intent;
import android.net.VpnService;
import android.os.Build;
import android.os.ParcelFileDescriptor;
import android.util.Log;

public class LadderService extends VpnService {

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    static final String TAG = MainActivity.class.getSimpleName();

    ParcelFileDescriptor parcelFileDescriptor;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "onStartCommand: ");

        String serverAddress = intent.getStringExtra(getString(R.string.server_address));
        String serverPort = intent.getStringExtra(getString(R.string.server_port));

        stop();
        String response = init(serverAddress, Integer.parseInt(serverPort));

        String[] parameters = response.split(" ");
        String sockfd = parameters[0];
        String address = parameters[1];
        String router = parameters[2];
        String dns0 = parameters[3];
        String dns1 = parameters[4];
        String dns2 = parameters[5];

        if (!protect(Integer.parseInt(sockfd))) {
            Log.d(TAG, "onStartCommand: failed to protect sockfd!");
        }

        Builder builder = new Builder();
        builder.addAddress(address, 32)
                .setMtu(1500)
                .addRoute(router, 0)
                .addDnsServer(dns0)
                .addDnsServer(dns1)
                .addDnsServer(dns2)
                .setSession("4over6");
        parcelFileDescriptor = builder.establish();
        int fd = parcelFileDescriptor.getFd();

        start(fd);

        return super.onStartCommand(intent, flags, startId);
    }

    @Override
    public void onDestroy() {
        stop();

        super.onDestroy();
    }

    public native String init(String serverAddress, int serverPort);

    public native void start(int tunnel);

    public native void stop();

}
