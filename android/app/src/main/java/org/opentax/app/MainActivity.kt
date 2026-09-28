package org.opentax.app

import android.content.pm.ApplicationInfo
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import org.opentax.app.ui.AppViewModel
import org.opentax.app.ui.OpenTaxApp
import org.opentax.app.ui.OpenTaxTheme

class MainActivity : ComponentActivity() {
    private val vm: AppViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Tax data: keep it out of screenshots, screen recordings and the recent-apps view.
        // Debuggable (development) builds allow screenshots so the UI can be checked.
        val debuggable = applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0
        if (!debuggable) window.setFlags(WindowManager.LayoutParams.FLAG_SECURE, WindowManager.LayoutParams.FLAG_SECURE)
        enableEdgeToEdge()
        setContent {
            OpenTaxTheme {
                OpenTaxApp(vm)
            }
        }
    }

    override fun onStop() {
        super.onStop()
        vm.saveNow()  // don't wait for the autosave delay when the app leaves the screen
    }
}
