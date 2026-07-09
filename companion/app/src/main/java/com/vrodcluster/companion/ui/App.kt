package com.vrodcluster.companion.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.material3.adaptive.navigationsuite.NavigationSuiteScaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.navigation.NavDestination.Companion.hasRoute
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.vrodcluster.companion.ui.theme.VRodTheme

/**
 * App root. A branded Expressive theme wraps an adaptive
 * [NavigationSuiteScaffold] (bottom bar on phones, rail on larger screens)
 * over a type-safe NavHost. The connection-status strip sits above the
 * content so link state is visible on every tab. Replaces the previous
 * hand-rolled three-screen `when` switch.
 */
@Composable
fun App() {
    VRodTheme(dynamicColor = AppPrefs.dynamicColor) {
        val navController = rememberNavController()
        val backStackEntry by navController.currentBackStackEntryAsState()
        val currentDestination = backStackEntry?.destination

        NavigationSuiteScaffold(
            navigationSuiteItems = {
                TOP_LEVEL_DESTINATIONS.forEach { dest ->
                    val selected = currentDestination?.hierarchy?.any {
                        it.hasRoute(dest.route::class)
                    } == true
                    item(
                        selected = selected,
                        onClick = { navController.navigateTopLevel(dest.route) },
                        icon = { Icon(dest.icon, contentDescription = dest.label) },
                        label = { Text(dest.label) },
                    )
                }
            },
        ) {
            Column(Modifier.fillMaxSize()) {
                ConnectionStatusBar(
                    onManage = { navController.navigateTopLevel(Cluster) },
                )
                NavHost(
                    navController = navController,
                    startDestination = Ride,
                    modifier = Modifier.fillMaxWidth().weight(1f),
                ) {
                    composable<Ride> {
                        RideScreen(onSetUpLink = { navController.navigateTopLevel(Cluster) })
                    }
                    composable<Cluster> {
                        ClusterScreen(onPickCluster = { navController.navigate(Scan) })
                    }
                    composable<Settings> {
                        SettingsScreen(onConfigureApps = { navController.navigate(AppList) })
                    }
                    composable<History> { HistoryScreen() }
                    composable<Scan> { ScanScreen(onBack = { navController.popBackStack() }) }
                    composable<AppList> { AppListScreen(onBack = { navController.popBackStack() }) }
                }
            }
        }
    }
}

/** Tab-style navigation: single copy on the back stack, state saved/restored. */
private fun NavHostController.navigateTopLevel(route: Any) {
    navigate(route) {
        popUpTo(graph.findStartDestination().id) { saveState = true }
        launchSingleTop = true
        restoreState = true
    }
}
