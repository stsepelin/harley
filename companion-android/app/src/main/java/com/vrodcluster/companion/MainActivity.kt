package com.vrodcluster.companion

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.vrodcluster.companion.ui.App

// Single-activity host. Real work lives in the foreground service +
// notification listener; the activity only shows status, exposes the
// permission-grant flow, and lets the user pick which apps forward.
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { App() }
    }
}
