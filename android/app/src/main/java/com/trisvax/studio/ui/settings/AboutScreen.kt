package com.trisvax.studio.ui.settings

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Movie
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.trisvax.studio.theme.TrisvaxColors

@Composable
fun AboutScreen(onBack: () -> Unit) {
    Column(Modifier.fillMaxSize().background(TrisvaxColors.MainBackground).safeDrawingPadding().padding(horizontal = 18.dp)) {
        Row(Modifier.fillMaxWidth().height(58.dp), verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = onBack) { Icon(Icons.Filled.ArrowBack, "Back", tint = TrisvaxColors.PrimaryText) }
            Text("About", color = TrisvaxColors.PrimaryText, fontSize = 24.sp, fontWeight = FontWeight.Bold)
        }
        Spacer(Modifier.height(24.dp))
        Surface(color = TrisvaxColors.PrimaryCard, shape = RoundedCornerShape(20.dp), border = BorderStroke(1.dp, TrisvaxColors.Border), modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(horizontal = 24.dp, vertical = 30.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Surface(color = TrisvaxColors.PrimaryOrange, shape = RoundedCornerShape(18.dp), modifier = Modifier.size(68.dp)) {
                    Box(contentAlignment = Alignment.Center) { Icon(Icons.Filled.Movie, null, tint = TrisvaxColors.MainBackground, modifier = Modifier.size(36.dp)) }
                }
                Text("TRISVAX STUDIO", color = TrisvaxColors.PrimaryText, fontSize = 20.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 1.sp)
                Text("Video editor", color = TrisvaxColors.SecondaryOrange, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                Text("Version 2.0.0-cpp", color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
                Text("A mobile-first video editing studio built around a native C++ editing engine and a focused Android interface.", color = TrisvaxColors.SecondaryText, fontSize = 13.sp, lineHeight = 20.sp, textAlign = TextAlign.Center)
            }
        }
        Spacer(Modifier.height(16.dp))
        Surface(color = TrisvaxColors.PrimaryCard, shape = RoundedCornerShape(14.dp), border = BorderStroke(1.dp, TrisvaxColors.Border), modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Made for creators", color = TrisvaxColors.PrimaryText, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                Text("Trim, arrange, adjust, add audio and text, then export locally. No watermark is added to the supported export.", color = TrisvaxColors.SecondaryText, fontSize = 12.sp, lineHeight = 18.sp)
            }
        }
    }
}
