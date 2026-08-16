<template>
  <!-- Locked design: no top bar — bare brand on the background,
       navigation lives in the floating bottom dock. -->
  <div class="topbrand">
    <a class="brand-name" href="./">
      <span class="brand-mark"><i class="fas fa-play"></i></span>
      Altagones
    </a>
  </div>

  <nav class="dock" aria-label="Pages">
    <a href="./"><i class="fas fa-fw fa-home"></i><span> {{ $t('navbar.home') }}</span></a>
    <a href="./pin"><i class="fas fa-fw fa-unlock"></i><span> {{ $t('navbar.pin') }}</span></a>
    <a href="./apps"><i class="fas fa-fw fa-stream"></i><span> {{ $t('navbar.applications') }}</span></a>
    <a href="./config"><i class="fas fa-fw fa-cog"></i><span> {{ $t('navbar.configuration') }}</span></a>
    <a href="./password"><i class="fas fa-fw fa-user-shield"></i><span> {{ $t('navbar.password') }}</span></a>
    <a href="./troubleshooting"><i class="fas fa-fw fa-info"></i><span> {{ $t('navbar.troubleshoot') }}</span></a>
  </nav>
</template>

<script>
export default {
  mounted() {
    let el = document.querySelector(".dock a[href='" + document.location.pathname + "']");
    if (!el && (document.location.pathname === "/" || document.location.pathname === "")) {
      el = document.querySelector(".dock a");   // home
    }
    if (el) el.classList.add("active")
  }
}
</script>

<style>
.form-control::placeholder {
  opacity: 0.5;
}
</style>
