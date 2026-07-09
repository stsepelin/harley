package ee.zeppl.companion.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.unit.dp
import androidx.navigation.NavDestination
import androidx.navigation.NavDestination.Companion.hasRoute
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.toRoute
import ee.zeppl.companion.ui.theme.ZepplTheme

/**
 * App root. A branded Expressive theme over a type-safe NavHost, with adaptive
 * navigation: a bottom bar on the folded cover / phones (compact width) and a
 * side rail on the unfolded inner screen (medium+). The connection-status
 * strip sits above the content so link state is visible on every tab.
 *
 * The rail/bar are the plain M3 components (not NavigationSuiteScaffold) so we
 * control icon size, item spacing, and the rail's top inset directly - larger
 * glove-friendly icons, roomier spacing, and no wasted top padding.
 */
@Composable
fun App() {
    ZepplTheme(dynamicColor = AppPrefs.dynamicColor) {
        Surface(color = MaterialTheme.colorScheme.background) {
            val navController = rememberNavController()
            val backStackEntry by navController.currentBackStackEntryAsState()
            val currentDestination = backStackEntry?.destination
            val wideNav = LocalConfiguration.current.screenWidthDp >= 600

            val destinations = TOP_LEVEL_DESTINATIONS +
                if (AppPrefs.developerMode) listOf(DEV_DESTINATION) else emptyList()

            if (wideNav) {
                Row(Modifier.fillMaxSize()) {
                    AppNavigationRail(destinations, currentDestination) {
                        navController.navigateTopLevel(it)
                    }
                    MainContent(navController, Modifier.weight(1f))
                }
            } else {
                Scaffold(
                    bottomBar = {
                        AppNavigationBar(destinations, currentDestination) {
                            navController.navigateTopLevel(it)
                        }
                    },
                ) { innerPadding ->
                    MainContent(navController, Modifier.padding(innerPadding))
                }
            }
        }
    }
}

@Composable
private fun MainContent(navController: NavHostController, modifier: Modifier = Modifier) {
    // The connection badge is baked into each page's header, so the content
    // area is just the NavHost - no separate status strip.
    NavHost(
        navController = navController,
        startDestination = Ride,
        modifier = modifier.fillMaxSize(),
    ) {
        composable<Ride> {
            RideScreen(onSetUpLink = { navController.navigateTopLevel(Cluster) })
        }
        composable<Cluster> {
            ClusterScreen(
                onPickCluster = { navController.navigate(Scan) },
                onOpenCluster = { navController.navigate(ClusterDetail(it)) },
            )
        }
        composable<ClusterDetail> { entry ->
            ClusterDetailScreen(
                address = entry.toRoute<ClusterDetail>().address,
                onBack = { navController.popBackStack() },
            )
        }
        composable<Settings> {
            SettingsScreen(onConfigureApps = { navController.navigate(AppList) })
        }
        composable<History> { HistoryScreen() }
        composable<Developer> {
            DeveloperScreen(onExit = {
                AppPrefs.developerMode = false
                navController.navigateTopLevel(Ride)
            })
        }
        composable<Scan> { ScanScreen(onBack = { navController.popBackStack() }) }
        composable<AppList> { AppListScreen(onBack = { navController.popBackStack() }) }
    }
}

@Composable
private fun AppNavigationRail(
    destinations: List<TopLevelDestination>,
    currentDestination: NavDestination?,
    onNavigate: (Any) -> Unit,
) {
    // Only clear the status bar - no extra top padding - and space the items
    // out generously with big glove-friendly icons.
    NavigationRail(
        modifier = Modifier.fillMaxHeight(),
        windowInsets = WindowInsets.statusBars,
    ) {
        Spacer(Modifier.height(12.dp))
        destinations.forEach { dest ->
            NavigationRailItem(
                selected = currentDestination.isOn(dest.route),
                onClick = { onNavigate(dest.route) },
                icon = {
                    Icon(dest.icon, contentDescription = dest.label, modifier = Modifier.size(30.dp))
                },
                label = { Text(dest.label) },
            )
            Spacer(Modifier.height(12.dp))
        }
    }
}

@Composable
private fun AppNavigationBar(
    destinations: List<TopLevelDestination>,
    currentDestination: NavDestination?,
    onNavigate: (Any) -> Unit,
) {
    NavigationBar {
        destinations.forEach { dest ->
            NavigationBarItem(
                selected = currentDestination.isOn(dest.route),
                onClick = { onNavigate(dest.route) },
                icon = {
                    Icon(dest.icon, contentDescription = dest.label, modifier = Modifier.size(28.dp))
                },
                label = { Text(dest.label) },
            )
        }
    }
}

private fun NavDestination?.isOn(route: Any): Boolean =
    this?.hierarchy?.any { it.hasRoute(route::class) } == true

/** Tab-style navigation: single copy on the back stack, state saved/restored. */
private fun NavHostController.navigateTopLevel(route: Any) {
    navigate(route) {
        popUpTo(graph.findStartDestination().id) { saveState = true }
        launchSingleTop = true
        restoreState = true
    }
}
