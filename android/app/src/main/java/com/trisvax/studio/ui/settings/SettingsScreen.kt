package com.trisvax.studio.ui.settings

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.DarkMode
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.HighQuality
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.trisvax.studio.theme.TrisvaxColors

@Composable
fun SettingsScreen(onBack: () -> Unit) {
    Column(Modifier.fillMaxSize().background(TrisvaxColors.MainBackground).safeDrawingPadding().padding(horizontal = 18.dp)) {
        Row(Modifier.fillMaxWidth().height(58.dp), verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = onBack) { Icon(Icons.Filled.ArrowBack, "Back", tint = TrisvaxColors.PrimaryText) }
            Text("Settings", color = TrisvaxColors.PrimaryText, fontSize = 24.sp, fontWeight = FontWeight.Bold)
        }
        Text("Your editing setup", color = TrisvaxColors.SecondaryText, fontSize = 13.sp)
        Spacer(Modifier.height(18.dp))
        SettingRow(Icons.Filled.DarkMode, "Appearance", "TRISVAX dark theme")
        SettingRow(Icons.Filled.HighQuality, "Export format", "MP4 · 1080p · 30 FPS")
        SettingRow(Icons.Filled.Folder, "Project storage", "Saved locally on this device")
        SettingRow(Icons.Filled.Lock, "Privacy", "Your media is not uploaded")
        Surface(color = TrisvaxColors.PrimaryCard, shape = RoundedCornerShape(14.dp), border = BorderStroke(1.dp, TrisvaxColors.Border), modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Local-first editing", color = TrisvaxColors.PrimaryText, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                Text("Project files and editing operations stay on your device. The current export profile uses the supported output settings.", color = TrisvaxColors.SecondaryText, fontSize = 12.sp, lineHeight = 18.sp)
            }
        }
    }
}

@Composable
private fun SettingRow(icon: ImageVector, title: String, value: String) {
    Surface(color = TrisvaxColors.PrimaryCard, shape = RoundedCornerShape(14.dp), border = BorderStroke(1.dp, TrisvaxColors.Border), modifier = Modifier.fillMaxWidth().padding(bottom = 10.dp)) {
        Row(Modifier.padding(horizontal = 14.dp, vertical = 15.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Surface(color = TrisvaxColors.SecondaryCard, shape = RoundedCornerShape(10.dp), modifier = Modifier.size(38.dp)) {
                Box(contentAlignment = Alignment.Center) { Icon(icon, null, tint = TrisvaxColors.SecondaryOrange, modifier = Modifier.size(19.dp)) }
            }
            Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
                Text(title, color = TrisvaxColors.PrimaryText, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                Text(value, color = TrisvaxColors.SecondaryText, fontSize = 12.sp)
            }
        }
    }
}
