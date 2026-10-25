using System.Linq;
using System.Net;
using ECS3DLauncher.Services;
using Xunit;

namespace ECS3DManagedTests;

public class LauncherJoinAddressTests
{
  // Link-local on purpose; built from bytes because no documentation range covers it.
  private static readonly string LinkLocal = new IPAddress(new byte[] { 169, 254, 7, 7 }).ToString();

  private static NetworkInterfaceInfo Nic(string name, NetworkInterfaceKind kind, bool up, params string[] addresses) =>
    new(name, kind, up, true, addresses.Select(IPAddress.Parse).ToList());

  private static NetworkInterfaceInfo NoGatewayNic(string name, NetworkInterfaceKind kind, params string[] addresses) =>
    new(name, kind, true, false, addresses.Select(IPAddress.Parse).ToList());

  [Fact]
  public void LanAddressesKeepOnlyUsableIpv4OnInterfacesThatAreUp()
  {
    var found = LanAddresses.From(
    [
      Nic("wlan0", NetworkInterfaceKind.Wireless, true, "192.0.2.24", "2001:db8::2", LinkLocal),
      Nic("eth0", NetworkInterfaceKind.Ethernet, true, "198.51.100.5"),
      Nic("eth1", NetworkInterfaceKind.Ethernet, false, "198.51.100.99"),
      Nic("lo", NetworkInterfaceKind.Loopback, true, "127.0.0.1"),
      Nic("tun0", NetworkInterfaceKind.Other, true, "127.0.1.1", "203.0.113.2"),
      Nic("v6only", NetworkInterfaceKind.Ethernet, true, "2001:db8::1"),
    ]);

    Assert.Equal(
      new[] { "Ethernet 198.51.100.5", "Wi-Fi 192.0.2.24", "tun0 203.0.113.2" },
      found.Select(item => $"{item.Label} {item.Address}").ToArray());
  }

  [Fact]
  public void InterfacesSharingALabelAreToldApartByName()
  {
    var found = LanAddresses.From(
    [
      Nic("eth0", NetworkInterfaceKind.Ethernet, true, "198.51.100.5"),
      Nic("eth1", NetworkInterfaceKind.Ethernet, true, "203.0.113.5"),
      Nic("wlan0", NetworkInterfaceKind.Wireless, true, "192.0.2.5"),
    ]);

    Assert.Equal(
      new[] { "Ethernet (eth0)", "Ethernet (eth1)", "Wi-Fi" },
      found.Select(item => item.Label).ToArray());
  }

  [Fact]
  public void LanAddressesOrderByLabelThenAddress()
  {
    var found = LanAddresses.From(
    [
      Nic("b", NetworkInterfaceKind.Other, true, "198.51.100.9", "198.51.100.10"),
      Nic("a", NetworkInterfaceKind.Other, true, "192.0.2.2"),
    ]);

    Assert.Equal(
      new[] { "192.0.2.2", "198.51.100.10", "198.51.100.9" },
      found.Select(item => item.Address).ToArray());
  }

  [Fact]
  public void InterfacesWithoutADefaultGatewayComeLast()
  {
    var found = LanAddresses.From(
    [
      NoGatewayNic("VirtualBox Host-Only Network", NetworkInterfaceKind.Ethernet, "192.0.2.1"),
      NoGatewayNic("vEthernet (Default Switch)", NetworkInterfaceKind.Ethernet, "198.51.100.1"),
      Nic("wlan0", NetworkInterfaceKind.Wireless, true, "203.0.113.24"),
    ]);

    Assert.Equal(
      new[] { "203.0.113.24", "192.0.2.1", "198.51.100.1" },
      found.Select(item => item.Address).ToArray());
  }

  [Fact]
  public void GatewayInterfaceLeadsEvenWhenItsLabelSortsLater()
  {
    var found = LanAddresses.From(
    [
      NoGatewayNic("vEthernet (WSL)", NetworkInterfaceKind.Other, "198.51.100.1"),
      Nic("wlan0", NetworkInterfaceKind.Wireless, true, "203.0.113.24"),
    ]);

    Assert.Equal("Wi-Fi", found[0].Label);
    Assert.Equal("vEthernet (WSL)", found[1].Label);
  }

  [Theory]
  [InlineData(" 192.0.2.24 ", "", "192.0.2.24", 3000)]
  [InlineData("192.0.2.24", "3001", "192.0.2.24", 3001)]
  [InlineData("host.local", " 65535 ", "host.local", 65535)]
  [InlineData("host.local", "1", "host.local", 1)]
  public void JoinTargetAcceptsValidInput(string address, string port, string expectedAddress, int expectedPort)
  {
    var result = JoinTarget.Parse(address, port, 3000);

    Assert.True(result.IsValid);
    Assert.Equal(new JoinTarget(expectedAddress, expectedPort), result.Target);
  }

  [Theory]
  [InlineData("host:", "3000", JoinTarget.InvalidAddressError)]
  [InlineData(":3000", "3000", JoinTarget.InvalidAddressError)]
  [InlineData("-host", "3000", JoinTarget.InvalidAddressError)]
  [InlineData("--port", "", JoinTarget.InvalidAddressError)]
  [InlineData("", "3000", JoinTarget.AddressError)]
  [InlineData("   ", "3000", JoinTarget.AddressError)]
  [InlineData("host", "0", JoinTarget.PortError)]
  [InlineData("host", "65536", JoinTarget.PortError)]
  [InlineData("host", "-1", JoinTarget.PortError)]
  [InlineData("host", "30.5", JoinTarget.PortError)]
  [InlineData("host", "abc", JoinTarget.PortError)]
  public void JoinTargetRejectsInvalidInput(string address, string port, string error)
  {
    var result = JoinTarget.Parse(address, port, 3000);

    Assert.False(result.IsValid);
    Assert.Equal(error, result.Error);
  }

  [Theory]
  [InlineData("192.0.2.24:3001", "192.0.2.24", "3001")]
  [InlineData("host.local:70000", "host.local", "70000")]
  [InlineData("  host.local:3001 ", "host.local", "3001")]
  [InlineData("[::1]:3000", "::1", "3000")]
  [InlineData("192.0.2.24", "192.0.2.24", null)]
  [InlineData("::1", "::1", null)]
  [InlineData("host:", "host:", null)]
  [InlineData(":3000", ":3000", null)]
  public void PastedAddressSplitsOnlyWhenItCarriesAPort(string pasted, string address, string? port)
  {
    Assert.Equal((address, port), JoinTarget.SplitPasted(pasted));
  }

  [Fact]
  public void BracketedAddressLosesItsBracketsEverywhere()
  {
    Assert.Equal(("::1", (string?)null), JoinTarget.SplitPasted("[::1]"));
    Assert.Equal("::1", JoinTarget.Parse("[::1]", "3000", 3000).Target?.Address);
  }

  [Fact]
  public void PastedOutOfRangePortIsRejectedByParse()
  {
    var (address, port) = JoinTarget.SplitPasted("host.local:70000");

    Assert.False(JoinTarget.Parse(address, port, 3000).IsValid);
  }
}
